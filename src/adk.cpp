#include "adk.h"
#include "model_db.h"

#include <windows.h>
#include <objbase.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <sstream>
#include <set>

namespace opendepot {

namespace {

constexpr uint16_t BE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
constexpr uint32_t BE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void PutBE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}
void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

std::string CStrField(const std::array<uint8_t, 800>& raw, size_t off, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && raw[off + n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(raw.data() + off), n);
}

bool ReadWholeStream(IStorage* stg, const wchar_t* name, std::vector<uint8_t>& out) {
    IStream* s = nullptr;
    if (FAILED(stg->OpenStream(name, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &s))) return false;
    STATSTG stat{};
    if (FAILED(s->Stat(&stat, STATFLAG_NONAME))) {
        s->Release();
        return false;
    }
    const ULONGLONG size = stat.cbSize.QuadPart;
    out.assign(static_cast<size_t>(size), 0);
    ULONG read = 0;
    HRESULT hr = size ? s->Read(out.data(), static_cast<ULONG>(size), &read) : S_OK;
    s->Release();
    return SUCCEEDED(hr) && read == size;
}

bool WriteWholeStream(IStorage* stg, const wchar_t* name, const std::vector<uint8_t>& data) {
    IStream* s = nullptr;
    if (FAILED(stg->CreateStream(name, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &s)))
        return false;
    ULARGE_INTEGER zero{};
    s->SetSize(zero);
    ULONG written = 0;
    HRESULT hr = data.empty() ? S_OK : s->Write(data.data(), static_cast<ULONG>(data.size()), &written);
    s->Release();
    return SUCCEEDED(hr) && written == data.size();
}

inline uint8_t Nxt(uint8_t s) { return static_cast<uint8_t>(s * 0x55 + 0x19); }

struct Rc4 {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    bool ok = false;

    bool Init(const std::vector<uint8_t>& seed) {
        if (seed.size() < 9) return false;
        if (!CryptAcquireContextA(&prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
                                  PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            return false;
        if (!CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) return false;
        if (!CryptHashData(hash, seed.data(), 9, 0)) return false;
        ok = true;
        return true;
    }
    ~Rc4() {
        if (hash) CryptDestroyHash(hash);
        if (prov) CryptReleaseContext(prov, 0);
    }
    bool Crypt(bool encrypt, const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
        if (!ok) return false;
        HCRYPTKEY key = 0;
        if (!CryptDeriveKey(prov, CALG_RC4, hash, 0, &key)) return false;
        bool result = false;
        if (encrypt) {
            DWORD need = static_cast<DWORD>(in.size());
            if (CryptEncrypt(key, 0, TRUE, 0, nullptr, &need, 0)) {
                std::vector<uint8_t> buf(need);
                if (!in.empty()) memcpy(buf.data(), in.data(), in.size());
                DWORD len = static_cast<DWORD>(in.size());
                if (CryptEncrypt(key, 0, TRUE, 0, buf.data(), &len, need)) {
                    buf.resize(len);
                    out.swap(buf);
                    result = true;
                }
            }
        } else {
            std::vector<uint8_t> buf = in;
            DWORD len = static_cast<DWORD>(buf.size());
            if (CryptDecrypt(key, 0, TRUE, 0, buf.empty() ? nullptr : buf.data(), &len)) {
                buf.resize(len);
                out.swap(buf);
                result = true;
            }
        }
        CryptDestroyKey(key);
        return result;
    }
};

bool EnumerateStreams(IStorage* stg, std::vector<std::pair<int, std::wstring>>& names) {
    IEnumSTATSTG* e = nullptr;
    if (FAILED(stg->EnumElements(0, nullptr, 0, &e))) return false;
    STATSTG stat{};
    ULONG fetched = 0;
    while (e->Next(1, &stat, &fetched) == S_OK && fetched == 1) {
        if (stat.type == STGTY_STREAM && stat.pwcsName) {
            int num = -1;
            if (swscanf(stat.pwcsName, L"Strm_%d", &num) == 1)
                names.emplace_back(num, stat.pwcsName);
        }
        if (stat.pwcsName) CoTaskMemFree(stat.pwcsName);
        memset(&stat, 0, sizeof(stat));
    }
    e->Release();
    std::sort(names.begin(), names.end(), [](auto& a, auto& b) { return a.first < b.first; });
    return true;
}

}  // namespace

uint16_t Crc16Ccitt(const uint8_t* p, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(p[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

bool AdkCustomDecrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    uint32_t K = 0;
    int state = 0, esc = 0, f3 = 0;
    uint8_t prng = 0;
    bool first = true;
    out.clear();
    for (uint8_t ch : in) {
        if (first) {
            state = 0; esc = 0; f3 = 0; K = 0; prng = 0; first = false;
        }
        if (state > 8) {
            uint8_t plain;
            if (esc) {
                esc = 0;
                plain = (ch == 0xE5) ? 0x1A : 0x1B;
            } else if (ch == 0x1B) {
                esc = 1;
                continue;
            } else {
                plain = static_cast<uint8_t>(ch ^ ((K >> 24) & 0xFF));
            }
            const uint8_t r = Nxt(prng);
            prng = r;
            K = (K & 0xFFFF00FFu) | (uint32_t(r) << 8);
            const uint8_t newB3 = static_cast<uint8_t>(plain + r + ((K >> 24) & 0xFF));
            K = (K & 0x00FFFFFFu) | (uint32_t(newB3) << 24);
            out.push_back(plain);
            continue;
        }
        if (state == 5) {
            K = (K & 0xFF00FFFFu) | (uint32_t(ch) << 16);
            ++state;
        } else if (state == 6) {
            state = 7;
            f3 = ch;
        } else if (state == 8) {
            const uint8_t u = static_cast<uint8_t>(((f3 + ((K >> 16) & 0xFF)) & 0xFF) - ch);
            K = (K & 0xFFFFFF00u) | u;
            prng = Nxt(u);
            K = (K & 0x00FFFFFFu) | (uint32_t(prng) << 24);
            ++state;
        } else {
            ++state;
        }
    }
    return true;
}

bool AdkCustomEncrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    uint8_t h[9];
    uint8_t s = static_cast<uint8_t>(GetTickCount() & 0xFF);
    for (int i = 0; i < 9; ++i) {
        s = Nxt(s);
        h[i] = s;
    }
    s = Nxt(s); h[5] = s; uint8_t b1 = s;
    s = Nxt(s); h[6] = s; uint8_t b2 = s;
    s = Nxt(s); h[8] = s;
    s = static_cast<uint8_t>(((b2 + b1) & 0xFF) - h[8]);
    s = Nxt(s);
    uint8_t ks = s;
    out.assign(h, h + 9);
    for (uint8_t ch : in) {
        uint8_t c = static_cast<uint8_t>(ks ^ ch);
        if (c == 0x1A || c == 0x1B) {
            out.push_back(0x1B);
            if (c == 0x1B) {
                const uint8_t saved = s;
                do {
                    s = Nxt(s);
                } while (s == 0xE5 || s == 0x1B || s == 0x1A);
                c = s;
                s = saved;
            } else {
                c = 0xE5;
            }
        }
        out.push_back(c);
        s = Nxt(s);
        ks = static_cast<uint8_t>(ks + s + ch);
    }
    return true;
}

bool Rc4Decrypt(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& in,
                std::vector<uint8_t>& out) {
    Rc4 rc4;
    return rc4.Init(seed) && rc4.Crypt(false, in, out);
}
bool Rc4Encrypt(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& in,
                std::vector<uint8_t>& out) {
    Rc4 rc4;
    return rc4.Init(seed) && rc4.Crypt(true, in, out);
}

bool LoadCodeplug(const std::wstring& path, Codeplug& cp) {
    cp = Codeplug{};
    IStorage* root = nullptr;
    if (FAILED(StgOpenStorage(path.c_str(), nullptr, STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, &root))) {
        cp.error = "Unable to open compound file";
        return false;
    }
    bool ok = false;
    do {
        std::vector<uint8_t> hdr;
        if (!ReadWholeStream(root, L"AdkFileHeader", hdr) || hdr.size() < 800) {
            cp.error = "Missing/short AdkFileHeader";
            break;
        }
        memcpy(cp.header.raw.data(), hdr.data(), 800);
        auto& H = cp.header;
        H.version = CStrField(H.raw, 0, 6);
        H.flag = H.raw[8];
        H.encType = (H.flag & 0x18) >> 3;
        H.adkType = H.flag & 7;
        H.model = CStrField(H.raw, 0x1C, 20);
        H.serial = CStrField(H.raw, 0x30, 20);
        memcpy(H.guid.data(), H.raw.data() + 0x0C, 16);
        cp.encType = H.encType;

        if (H.encType == 1) {
            // Depot codeplugs use the fixed RC4 seed "A8Z6J9QP5". The 0x239 key
            // blob is the legacy-cipher encryption of that seed; some blobs contain
            // escape bytes our legacy decoder mishandles, so use the constant.
            const size_t len = H.raw[0x239];
            if (len && 0x23A + len <= H.raw.size())
                cp.keyBlob.assign(H.raw.begin() + 0x23A, H.raw.begin() + 0x23A + len);
            const char kSeed[] = "A8Z6J9QP5";
            cp.seed.assign(kSeed, kSeed + 9);
        }

        IStorage* dataStg = nullptr;
        if (FAILED(root->OpenStorage(L"DataStg", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &dataStg))) {
            cp.error = "Missing DataStg storage";
            break;
        }
        std::vector<std::pair<int, std::wstring>> names;
        if (!EnumerateStreams(dataStg, names) || names.empty()) {
            dataStg->Release();
            cp.error = "No Strm_* streams";
            break;
        }

        auto decryptStream = [&](const std::vector<uint8_t>& raw, std::vector<uint8_t>& plain) -> bool {
            if (H.encType == 1) return Rc4Decrypt(cp.seed, raw, plain);
            if (H.encType == 0) return AdkCustomDecrypt(raw, plain);
            plain = raw;
            return true;
        };

        std::vector<uint8_t> mapRaw;
        bool haveMap = false;
        struct MapVal { uint16_t type, checksum, stream; };
        std::vector<MapVal> mapVals;
        std::vector<uint32_t> mapIds;
        std::vector<std::vector<uint8_t>> plains;
        std::vector<int> nums;

        for (auto& entry : names) {
            std::vector<uint8_t> raw, plain;
            if (!ReadWholeStream(dataStg, entry.second.c_str(), raw)) {
                cp.error = "Failed to read stream";
                break;
            }
            if (!decryptStream(raw, plain)) {
                cp.error = "Failed to decrypt stream";
                break;
            }
            if (entry.first == 0) {
                mapRaw = plain;
                haveMap = true;
            } else {
                plains.push_back(std::move(plain));
                nums.push_back(entry.first);
            }
        }
        dataStg->Release();
        if (!cp.error.empty()) break;
        if (!haveMap || mapRaw.size() < 12) {
            cp.error = "Missing/short Strm_0 directory";
            break;
        }
        cp.mapPlain = mapRaw;
        const size_t body = mapRaw.size() - 2;
        if (body % 10 != 0) {
            cp.error = "Directory size is not a multiple of 10";
            break;
        }
        for (size_t o = 0; o + 10 <= body; o += 10) {
            mapIds.push_back(BE32(&mapRaw[o]));
            mapVals.push_back({BE16(&mapRaw[o + 4]), BE16(&mapRaw[o + 6]), BE16(&mapRaw[o + 8])});
        }

        for (size_t i = 0; i < plains.size(); ++i) {
            const auto& d = plains[i];
            if (d.size() < 8) continue;
            Record r;
            r.raw = d;
            r.id = BE32(d.data());
            r.streamNum = nums[i];
            const uint16_t len = BE16(&d[4]);
            r.checksum = BE16(&d[d.size() - 2]);
            r.verified = (len == d.size() - 6);
            auto it = std::find(mapIds.begin(), mapIds.end(), r.id);
            if (it != mapIds.end()) {
                size_t idx = size_t(it - mapIds.begin());
                r.type = mapVals[idx].type;
                r.verified = r.verified && mapVals[idx].checksum == r.checksum &&
                             mapVals[idx].stream == r.streamNum;
            } else {
                r.verified = false;
            }
            cp.records.push_back(std::move(r));
        }
        std::sort(cp.records.begin(), cp.records.end(),
                  [](const Record& a, const Record& b) { return a.streamNum < b.streamNum; });
        ok = true;
    } while (false);

    root->Release();
    if (!ok && cp.error.empty()) cp.error = "Unknown load error";
    return ok;
}

bool SaveCodeplug(const std::wstring& path, const Codeplug& cp, std::string& err) {
    err.clear();
    if (cp.records.empty()) {
        err = "No records to save";
        return false;
    }
    const int encType = cp.encType;
    if (encType == 1 && cp.seed.size() < 9) {
        err = "Missing RC4 seed";
        return false;
    }

    auto encryptStream = [&](const std::vector<uint8_t>& plain, std::vector<uint8_t>& enc) -> bool {
        if (encType == 1) return Rc4Encrypt(cp.seed, plain, enc);
        if (encType == 0) return AdkCustomEncrypt(plain, enc);
        enc = plain;
        return true;
    };

    std::vector<uint8_t> map;
    std::vector<std::vector<uint8_t>> plains;
    plains.reserve(cp.records.size());
    for (size_t i = 0; i < cp.records.size(); ++i) {
        Record r = cp.records[i];
        if (r.raw.size() < 8) {
            err = "Record too short";
            return false;
        }
        r.id = BE32(r.raw.data());
        const uint16_t len = static_cast<uint16_t>(r.raw.size() - 6);
        r.raw[4] = uint8_t(len >> 8);
        r.raw[5] = uint8_t(len);
        const uint16_t csum = Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8);
        r.raw[r.raw.size() - 2] = uint8_t(csum >> 8);
        r.raw[r.raw.size() - 1] = uint8_t(csum);
        PutBE32(map, r.id);
        PutBE16(map, r.type);
        PutBE16(map, csum);
        PutBE16(map, static_cast<uint16_t>(i + 1));
        plains.push_back(std::move(r.raw));
    }
    uint32_t sum = 0;
    for (uint8_t b : map) sum += b;
    PutBE16(map, static_cast<uint16_t>(sum + 0xAAAB));

    std::array<uint8_t, 800> hdr = cp.header.raw;
    uint32_t hsum = 0;
    for (size_t i = 0; i <= 0x40; ++i) hsum += hdr[i];
    hdr[0x44] = static_cast<uint8_t>(0x5A - hsum);

    std::vector<uint8_t> mapEnc;
    if (!encryptStream(map, mapEnc)) {
        err = "Failed to encrypt directory";
        return false;
    }

    IStorage* root = nullptr;
    if (FAILED(StgCreateDocfile(path.c_str(),
                                STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &root))) {
        err = "Unable to create compound file";
        return false;
    }
    bool ok = false;
    do {
        std::vector<uint8_t> headerVec(hdr.begin(), hdr.end());
        if (!WriteWholeStream(root, L"AdkFileHeader", headerVec)) {
            err = "Failed to write AdkFileHeader";
            break;
        }
        IStorage* dataStg = nullptr;
        if (FAILED(root->CreateStorage(L"DataStg",
                                       STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &dataStg))) {
            err = "Failed to create DataStg";
            break;
        }
        bool wrote = WriteWholeStream(dataStg, L"Strm_0", mapEnc);
        for (size_t i = 0; i < plains.size() && wrote; ++i) {
            std::vector<uint8_t> enc;
            if (!encryptStream(plains[i], enc)) {
                wrote = false;
                break;
            }
            wchar_t name[32];
            swprintf(name, 32, L"Strm_%zu", i + 1);
            wrote = WriteWholeStream(dataStg, name, enc);
        }
        if (wrote) dataStg->Commit(STGC_DEFAULT);
        dataStg->Release();
        if (!wrote) {
            err = "Failed to write data stream";
            break;
        }
        root->Commit(STGC_DEFAULT);
        ok = true;
    } while (false);
    root->Release();
    return ok;
}

std::string VerifyCodeplug(const Codeplug& cp) {
    std::ostringstream os;
    size_t good = 0, bad = 0;
    for (const auto& r : cp.records) {
        if (r.verified) ++good; else ++bad;
    }
    os << "model=" << cp.header.model << " serial=" << cp.header.serial
       << " encType=" << cp.encType << " adkType=" << cp.header.adkType << "\n";
    if (cp.seed.size() >= 9)
        os << "seed=" << std::string(reinterpret_cast<const char*>(cp.seed.data()), 9) << "\n";
    os << "records=" << cp.records.size() << " verified=" << good << " bad=" << bad << "\n";
    for (const auto& r : cp.records) {
        if (!r.verified) {
            char buf[96];
            snprintf(buf, sizeof(buf), "  BAD id=0x%08X stream=%d size=%zu\n",
                     r.id, r.streamNum, r.raw.size());
            os << buf;
        }
    }
    return os.str();
}

bool ReadIdentityFromCodeplug(const Codeplug& cp, std::string& serial, std::string& model) {
    const Record* r = nullptr;
    for (const auto& x : cp.records) if (x.id == 0x10040000u) { r = &x; break; }
    if (!r || r->raw.size() < 12) return false;
    const uint8_t* p = r->raw.data() + 6;
    size_t n = r->raw.size() - 6 - 2;  // exclude trailing CRC
    size_t o = 2;
    serial.clear();
    model.clear();
    while (o < n && p[o]) serial.push_back(char(p[o++]));
    while (o < n && p[o] == 0) ++o;
    while (o < n && p[o]) model.push_back(char(p[o++]));
    return !serial.empty() && !model.empty();
}

bool UpdateIdentity(Codeplug& cp, const std::string& serial, const std::string& model) {
    if (serial.size() > 19 || model.size() > 19) return false;
    const std::string oldSerial = cp.header.serial;
    const std::string oldModel = cp.header.model;

    memset(cp.header.raw.data() + 0x30, 0, 20);
    memcpy(cp.header.raw.data() + 0x30, serial.data(), serial.size());
    memset(cp.header.raw.data() + 0x1C, 0, 20);
    memcpy(cp.header.raw.data() + 0x1C, model.data(), model.size());
    cp.header.serial = serial;
    cp.header.model = model;

    auto replace = [](std::vector<uint8_t>& raw, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        auto it = std::search(raw.begin(), raw.end(), from.begin(), from.end());
        if (it == raw.end()) return;
        size_t off = size_t(it - raw.begin());
        size_t n = to.size();
        std::copy(to.begin(), to.end(), raw.begin() + off);
        for (size_t k = n; k < from.size(); ++k) raw[off + k] = 0;
    };
    for (auto& r : cp.records) {
        if (r.id != 0x10040000) continue;
        replace(r.raw, oldSerial, serial);
        replace(r.raw, oldModel, model);
    }
    return true;
}

bool ValidateRadioCodeplug(const Codeplug& cp, std::string& err) {
    err.clear();
    if (cp.records.empty()) {
        err = "codeplug has no records";
        return false;
    }
    const uint32_t ids[3] = {0x10040000u, 0x10050000u, 0x10060000u};
    const Record* idrec[3] = {nullptr, nullptr, nullptr};
    for (const auto& r : cp.records) {
        for (int i = 0; i < 3; ++i)
            if (r.id == ids[i]) idrec[i] = &r;
        if (r.type == 0x0082) {
            err = "codeplug contains partition 0x0082 (calibration/tuning); refusing to write";
            return false;
        }
        if (r.raw.size() < 8) {
            char b[80];
            snprintf(b, sizeof(b), "record 0x%08X is too short", r.id);
            err = b;
            return false;
        }
        const uint16_t len = BE16(&r.raw[4]);
        if (len != r.raw.size() - 6) {
            char b[112];
            snprintf(b, sizeof(b), "record 0x%08X length field %u != size-6 %zu", r.id, len,
                     r.raw.size() - 6);
            err = b;
            return false;
        }
        const uint16_t crc = BE16(&r.raw[r.raw.size() - 2]);
        if (Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8) != crc) {
            char b[96];
            snprintf(b, sizeof(b), "record 0x%08X has a bad CRC", r.id);
            err = b;
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!idrec[i]) {
            char b[160];
            snprintf(b, sizeof(b),
                     "missing identity record 0x%08X (partition 0x0081); writing this would "
                     "blank the radio (FAIL 01/92)",
                     ids[i]);
            err = b;
            return false;
        }
        if (idrec[i]->type != 0x0081) {
            char b[112];
            snprintf(b, sizeof(b), "identity record 0x%08X is not in partition 0x0081", ids[i]);
            err = b;
            return false;
        }
    }
    if (cp.header.model.empty() || cp.header.serial.empty()) {
        err = "codeplug header has an empty model or serial";
        return false;
    }
    if (cp.header.model.size() > 19 || cp.header.serial.size() > 19) {
        err = "model/serial longer than 19 bytes";
        return false;
    }
    const auto& raw = idrec[0]->raw;
    auto contains = [&](const std::string& s) {
        return !s.empty() &&
               std::search(raw.begin() + 6, raw.end(), s.begin(), s.end()) != raw.end();
    };
    if (!contains(cp.header.serial)) {
        err = "header serial not found inside identity record 0x10040000";
        return false;
    }
    if (!contains(cp.header.model)) {
        err = "header model not found inside identity record 0x10040000";
        return false;
    }
    return true;
}

const std::vector<FeatureInfo>& FeatureMatrix() {
    static const std::vector<FeatureInfo> kMatrix = {
        {0, 8, "SmartZone Systems Software"}, {0, 4, "ASTRO Digital Operation"},
        {0, 2, "Conventional Systems Software"}, {0, 1, "SmartZone Omnilink"},
        {1, 8, "Enhanced Digital ID Display"}, {1, 4, "Hardware Multikey"},
        {1, 2, "SmartNet Systems Software"}, {1, 1, "Conventional Voting Scan"},
        {2, 8, "Soft ID"}, {2, 4, "StatAlert (MDC-1200)"},
        {2, 2, "Junior OTAR"}, {2, 1, "Tactical OTAR"},
        {3, 8, "Enhanced Secure Operation"}, {3, 4, "APCO Packet Data Interface"},
        {3, 2, "Emergency Call"}, {3, 1, "Dual Priority Scan"},
        {4, 8, "Radio Control Protocol"}, {4, 4, "Over the Air Scanning"},
        {4, 2, "Mixed Mode Operation"}, {4, 1, "Fireground Accountability"},
        {5, 8, "Front Panel Programming"}, {5, 4, "Fireground Voice"},
        {5, 2, "P25 Digital CAI"}, {5, 1, "Federal Government FPP"},
        {6, 8, "One-Touch Status/Message"}, {6, 4, "Multikey Operation (48 key)"},
        {6, 2, "Enhanced Trunking"}, {6, 1, "Direct Frequency Entry"},
        {7, 8, "Advanced System Key"}, {7, 4, "Enhanced Zone/Channel"},
        {7, 2, "Digital Tone Signaling"}, {7, 1, "Enhanced Channel Scan"},
        {8, 8, "Selective Call Decode"}, {8, 4, "Motorola ADP Encryption"},
        {8, 2, "Software System Key"}, {8, 1, "Unit-to-Unit Call"},
        {9, 8, "DES / DES-XL"}, {9, 4, "AES-256 Encryption"},
        {9, 2, "Call Alert Decode"}, {9, 1, "Secondary P25 Control"},
        {10, 8, "12.5 kHz Channel Spacing"}, {10, 4, "25.0 kHz Channel Spacing"},
        {10, 2, "Private Conversation"}, {10, 1, "ASTRO 25 TDMA Phase 2"},
        {11, 8, "Dynamic Dual Mode"}, {11, 4, "Emergency Alarm & Voice"},
        {11, 2, "Radio Inhibit (Stun/Kill)"}, {11, 1, "Failsoft Operation"},
    };
    return kMatrix;
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string CanonicalFlashcode(const std::string& input) {
    std::string hex;
    for (char c : input) {
        if (HexVal(c) >= 0) hex.push_back(char(toupper(static_cast<unsigned char>(c))));
        if (hex.size() == 12) break;
    }
    if (hex.size() != 12) return {};
    int total = 0;
    for (int i = 0; i < 12; ++i) {
        int n = HexVal(hex[i]);
        int d = (i % 2 == 0) ? 2 * n : n;
        total += d / 10 + d % 10;
    }
    int check = (10 - total % 10) % 10;
    return hex.substr(0, 6) + "-" + hex.substr(6, 6) + "-" + char('0' + check);
}

std::string FlashcodeToNibbles(const std::string& flashcode) {
    std::string hex;
    for (char c : flashcode) {
        if (HexVal(c) >= 0) hex.push_back(char(toupper(static_cast<unsigned char>(c))));
        if (hex.size() == 12) break;
    }
    return hex;
}

const FlashcodePlatform* FindFlashcodePlatform(const std::string& name) {
    for (const auto& p : FlashcodePlatforms())
        if (p.name == name) return &p;
    return nullptr;
}

std::string PlatformForRadio(const std::string& radio) {
    if (radio == "XTS2500") return "Astro_XTS2500";
    if (radio == "XTS5000") return "Astro_XTS5000";
    if (radio == "SSE5000") return "Astro_SSE5000";
    if (radio == "XTS1500") return "Astro_XTS1500";
    if (radio == "MT1500") return "Astro_MT1500";
    if (radio == "PR1500") return "Astro_PR1500";
    if (radio == "XTS4000") return "Astro_XTS5000";
    if (radio == "XTS2000/ATS3000") return "Astro_XTS3000";
    if (radio == "XTL5000") return "Astro_XTL5000";
    if (radio == "XTL2500") return "Astro_XTL2500";
    if (radio == "XTL1500") return "Astro_XTL1500";
    if (radio == "PM1500") return "Astro_PM1500";
    if (radio == "Astro Spectra Plus" || radio == "SPECTRA_PLUS") return "Astro_SPECTRA_PLUS";
    if (radio == "Astro Saber") return "Astro_Saber";
    if (radio == "XTS3000") return "Astro_XTS3000";
    if (FindFlashcodePlatform(radio)) return radio;
    return {};
}

std::string BuildFlashcode(const std::string& platform, const std::string& baseFlashcode,
                           const std::vector<std::string>& selectedCodes) {
    const FlashcodePlatform* p = FindFlashcodePlatform(platform);
    if (!p) return {};
    std::string hex = FlashcodeToNibbles(baseFlashcode);
    if (hex.size() != 12) hex.assign(12, '0');
    std::set<std::string> sel(selectedCodes.begin(), selectedCodes.end());
    for (const auto& o : p->options) {
        if (o.nibble < 0 || o.nibble > 11 || o.bit < 0 || o.bit > 3) continue;
        int v = HexVal(hex[o.nibble]);
        const int mask = 1 << o.bit;
        if (sel.count(o.code))
            v |= mask;
        else
            v &= ~mask;
        hex[o.nibble] = "0123456789ABCDEF"[v & 0x0F];
    }
    return CanonicalFlashcode(hex);
}

std::vector<std::string> DecodeFlashcode(const std::string& platform,
                                         const std::string& flashcode) {
    std::vector<std::string> out;
    const FlashcodePlatform* p = FindFlashcodePlatform(platform);
    if (!p) return out;
    std::string hex = FlashcodeToNibbles(flashcode);
    if (hex.size() != 12) return out;
    for (const auto& o : p->options) {
        if (o.nibble < 0 || o.nibble > 11 || o.bit < 0 || o.bit > 3) continue;
        if ((HexVal(hex[o.nibble]) >> o.bit) & 1) out.push_back(o.code + "  " + o.label);
    }
    return out;
}

static Record* FindRecord(Codeplug& cp, uint16_t tag, uint16_t inst) {
    for (auto& r : cp.records)
        if ((r.id >> 16) == tag && (r.id & 0xFFFF) == inst) return &r;
    return nullptr;
}
static const Record* FindRecord(const Codeplug& cp, uint16_t tag, uint16_t inst) {
    for (const auto& r : cp.records)
        if ((r.id >> 16) == tag && (r.id & 0xFFFF) == inst) return &r;
    return nullptr;
}

bool SetItemPayload(Codeplug& cp, uint16_t tag, uint16_t inst, const std::vector<uint8_t>& payload) {
    Record* r = FindRecord(cp, tag, inst);
    if (!r) return false;
    r->raw.resize(6 + payload.size() + 2);
    r->raw[0] = uint8_t(tag >> 8);
    r->raw[1] = uint8_t(tag);
    r->raw[2] = uint8_t(inst >> 8);
    r->raw[3] = uint8_t(inst);
    const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
    r->raw[4] = uint8_t(len >> 8);
    r->raw[5] = uint8_t(len);
    if (!payload.empty()) memcpy(r->raw.data() + 6, payload.data(), payload.size());
    const uint16_t crc = Crc16Ccitt(payload.data(), payload.size());
    r->raw[6 + payload.size()] = uint8_t(crc >> 8);
    r->raw[7 + payload.size()] = uint8_t(crc);
    r->checksum = crc;
    r->verified = true;
    return true;
}

std::string GetFlashcode(const Codeplug& cp) {
    const Record* r = FindRecord(cp, 0x1006, 0);
    if (!r || r->raw.size() < 6 + 24) return {};
    return std::string(reinterpret_cast<const char*>(r->raw.data() + 6 + 8), 15);
}

bool SetFlashcode(Codeplug& cp, const std::string& payloadOrFull, std::string& canonical,
                  std::string& err) {
    canonical = CanonicalFlashcode(payloadOrFull);
    if (canonical.empty()) {
        err = "FLASHcode must contain 12 hex nibbles";
        return false;
    }
    Record* r = FindRecord(cp, 0x1006, 0);
    if (!r || r->raw.size() < 6 + 24) {
        err = "No 0x1006 FLASHcode item";
        return false;
    }
    std::vector<uint8_t> payload(r->raw.begin() + 6, r->raw.end() - 2);
    if (payload.size() < 24) payload.resize(24, 0);
    memcpy(payload.data() + 8, canonical.data(), 15);
    payload[23] = 0;
    if (!SetItemPayload(cp, 0x1006, 0, payload)) {
        err = "Failed to update FLASHcode item";
        return false;
    }
    return true;
}

bool SetFlashcodeMap(Codeplug& cp, const std::string& payloadOrFull, std::string& canonical,
                     std::string& err) {
    canonical = CanonicalFlashcode(payloadOrFull);
    if (canonical.empty()) {
        err = "FLASHcode must contain 12 hex nibbles";
        return false;
    }
    std::string hex = FlashcodeToNibbles(canonical);
    if (hex.size() != 12) {
        err = "FLASHcode nibble extraction failed";
        return false;
    }
    Record* r5 = FindRecord(cp, 0x1005, 0);
    if (!r5 || r5->raw.size() < 6 + 12 + 2) {
        err = "No 0x1005 feature map item";
        return false;
    }
    const size_t end = r5->raw.size() - 2;
    for (int i = 0; i < 12; ++i) r5->raw[end - 12 + i] = uint8_t(HexVal(hex[i]) & 0x0F);
    const uint16_t c5 = Crc16Ccitt(r5->raw.data() + 6, r5->raw.size() - 8);
    r5->raw[r5->raw.size() - 2] = uint8_t(c5 >> 8);
    r5->raw[r5->raw.size() - 1] = uint8_t(c5);
    r5->checksum = c5;
    r5->verified = true;
    return SetFlashcode(cp, hex, canonical, err);
}

std::vector<std::string> EnabledFeatures(const std::string& canonical) {
    std::vector<std::string> out;
    std::string hex;
    for (char c : canonical) {
        if (HexVal(c) >= 0) hex.push_back(char(toupper(static_cast<unsigned char>(c))));
        if (hex.size() == 12) break;
    }
    if (hex.size() != 12) return out;
    for (int i = 0; i < 12; ++i) {
        int v = HexVal(hex[i]);
        for (int bit : {8, 4, 2, 1}) {
            if (!(v & bit)) continue;
            bool found = false;
            for (const auto& f : FeatureMatrix()) {
                if (f.nibble == i && f.bit == bit) {
                    out.push_back(f.name);
                    found = true;
                    break;
                }
            }
            if (!found) out.push_back("Option " + std::to_string(i) + "/" + std::to_string(bit));
        }
    }
    return out;
}

const std::vector<std::string>& BandOptions() {
    static const std::vector<std::string> v = {"800 MHz", "VHF", "UHF", "900 MHz"};
    return v;
}
const std::vector<std::string>& RadioOptions() {
    static const std::vector<std::string> v = {"XTS2500", "XTS5000", "SSE5000", "XTS1500",
                                               "MT1500",  "PR1500",  "XTS4000", "XTL5000", "XTS2000/ATS3000"};
    return v;
}
const std::vector<std::string>& TypeOptions() {
    static const std::vector<std::string> v = {"Model I", "Model II", "Model III", "ALL"};
    return v;
}
const std::vector<ModelInfo>& ModelCatalog() {
    static const std::vector<ModelInfo> v = {
        {"UHF", "XTS5000", "Model III", "H46UCF9PW6BN"},
        {"UHF", "XTS5000", "Model II", "H46UCF9PW6AN"},
        {"UHF", "XTS5000", "Model I", "H46UCF9PW6BN"},
    };
    return v;
}
std::vector<std::string> PackagesForModel(const std::string&) {
    return {"3600 Analog Trunk Data", "3600 Digital Trunk Data", "9600 Digital Trunk Data",
            "P25 MDC Conv Data", "Analog Cnv Data"};
}
std::vector<std::string> AddonsForModel(const std::string&) {
    return {"Q387", "H43", "G857", "H37", "H869", "Q498", "Q53",
            "H46",  "H04", "H02",  "Q445", "H345", "Q507", "Q369"};
}

std::string OptionLabel(const std::string& code) {
    struct OptionName { const char* code; const char* label; };
    static const OptionName kLabels[] = {
        {"G369", "Inter System Roaming (ISSI)"},
        {"G440", "GPS Operation"},
        {"G474", "Multi Radio SW"},
        {"G788", "9600 Baud Trunking"},
        {"G789", "3600 Analog or Digital Baud Trunking"},
        {"G790", "P25 Digital Conventional"},
        {"G791", "Mantaray Express Software Package"},
        {"G809", "3600 Analog Baud Trunking"},
        {"G857", "FDNY Emer Tx/Rx Tones"},
        {"G858", "HPD Operation"},
        {"H02", "Encrypted Tactical Inhibit"},
        {"H04", "Radio Lock"},
        {"H13", "Alphanumeric Messaging"},
        {"H14", "Digital ID Display"},
        {"H15", "ASTRO Data"},
        {"H35", "Conventional Operation"},
        {"H37", "SmartNet Operation"},
        {"H38", "SmartZone Operation"},
        {"H39", "Select Radio Inhibit"},
        {"H42", "Trk Var Tlkgrp ID"},
        {"H43", "Remote Mon Radio Trace"},
        {"H44", "Trk Data Operation"},
        {"H46", "TrkI Button Stat Msg"},
        {"H101", "Stat Alert Rep Access"},
        {"H345", "P25 Radio Authentication"},
        {"H806", "IMBE"},
        {"H868", "OTAR and Multikey"},
        {"H869", "Multikey"},
        {"Q52", "Conventional FPP"},
        {"Q53", "Conventional Radio Cloning"},
        {"Q173", "Omnilink"},
        {"Q182", "Enhanced RCP"},
        {"Q190", "Channel Change ID"},
        {"Q351", "Modat"},
        {"Q352", "Soft ID"},
        {"Q353", "Otacr"},
        {"Q354", "Otacs"},
        {"Q361", "APCO Trunking"},
        {"Q369", "Inter System Roaming (ISSI)"},
        {"Q387", "Conventional Voting Scan"},
        {"Q443", "3600 Analog Baud Trunking"},
        {"Q445", "Fireground Support"},
        {"Q446", "Fireground Voice Support"},
        {"Q507", "12.5 kHz FCC Mandate"},
        {"Q574", "9600 Baud Trunking"},
        {"Q575", "3600 Analog or Digital Baud Trunking"},
        {"Q667", "ADP"},
        {"Q811", "P25 Digital Conventional"},
        {"Q833", "MDC 1200 Analog Conventional"},
        {"Q883", "XTS2500 Mixed Mode Signaling"},
        {"Q947", "APCO Packet Data"},
        {"W12", "Preamp"},
        {"W357", "Scan with VRS"},
    };
    for (const auto& e : kLabels)
        if (code == e.code) return e.label;
    return code;
}

// Build a fresh codeplug record set from OpenDepot's own generated model database
// (no .cpg file is shipped or read).
bool BuiltinTemplate(const std::string& model, Codeplug& out, std::string& err) {
    err.clear();
    const ModelBlob* mb = nullptr;
    for (int i = 0; i < kModelCount; ++i)
        if (model == kModels[i].model) { mb = &kModels[i]; break; }
    if (!mb) {
        err = "no built-in schema for model " + model;
        return false;
    }
    out = Codeplug();
    // Header (AdkFileHeader) + parsed fields, same as LoadCodeplug.
    std::memcpy(out.header.raw.data(), mb->header, 800);
    auto& H = out.header;
    H.version = CStrField(H.raw, 0, 6);
    H.flag = H.raw[8];
    H.encType = (H.flag & 0x18) >> 3;
    H.adkType = H.flag & 7;
    H.model = CStrField(H.raw, 0x1C, 20);
    H.serial = CStrField(H.raw, 0x30, 20);
    std::memcpy(H.guid.data(), H.raw.data() + 0x0C, 16);
    out.encType = H.encType;
    if (H.encType == 1) {
        size_t klen = H.raw[0x239];
        if (klen && 0x23A + klen <= H.raw.size())
            out.keyBlob.assign(H.raw.begin() + 0x23A, H.raw.begin() + 0x23A + klen);
        const char kSeed[] = "A8Z6J9QP5";
        out.seed.assign(kSeed, kSeed + 9);
    }
    out.header.model = model;
    const uint8_t* d = mb->data;
    size_t o = 0;
    while (o + 8 <= mb->size) {
        uint32_t id = (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) |
                      (uint32_t(d[o + 2]) << 8) | d[o + 3];
        uint16_t part = uint16_t((d[o + 4] << 8) | d[o + 5]);
        uint16_t len = uint16_t((d[o + 6] << 8) | d[o + 7]);
        o += 8;
        if (o + len > mb->size) { err = "corrupt built-in schema"; return false; }
        Record r;
        r.id = id;
        r.type = part;
        r.raw.resize(size_t(6) + len);
        r.raw[0] = uint8_t(id >> 24); r.raw[1] = uint8_t(id >> 16);
        r.raw[2] = uint8_t(id >> 8);  r.raw[3] = uint8_t(id);
        r.raw[4] = uint8_t(len >> 8); r.raw[5] = uint8_t(len);
        std::memcpy(r.raw.data() + 6, d + o, len);
        r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw[r.raw.size() - 1]);
        r.verified = true;
        out.records.push_back(std::move(r));
        o += len;
    }
    if (out.records.empty()) { err = "empty built-in schema"; return false; }
    return true;
}

std::string BuiltinFirstModel() {
    return kModelCount > 0 ? std::string(kModels[0].model) : std::string();
}

void BuiltinReadRequests(std::vector<ReadRequest>& out) {
    out.clear();
    for (int i = 0; i < kModelCount; ++i) {
        const ModelBlob& mb = kModels[i];
        size_t o = 0;
        while (o + 8 <= mb.size) {
            uint32_t id = (uint32_t(mb.data[o]) << 24) | (uint32_t(mb.data[o + 1]) << 16) |
                          (uint32_t(mb.data[o + 2]) << 8) | mb.data[o + 3];
            uint16_t part = uint16_t((mb.data[o + 4] << 8) | mb.data[o + 5]);
            uint16_t len = uint16_t((mb.data[o + 6] << 8) | mb.data[o + 7]);
            o += 8;
            if (o + len > mb.size) break;
            const uint16_t tag = uint16_t(id >> 16), inst = uint16_t(id & 0xFFFF);
            bool found = false;
            for (auto& r : out)
                if (r.tag == tag && r.inst == inst) {
                    if (len > r.len) r.len = len;
                    found = true;
                    break;
                }
            if (!found) out.push_back(ReadRequest{part, tag, inst, len});
            o += len;
        }
    }
}

void BuiltinTagList(std::vector<std::pair<uint16_t, uint16_t>>& out) {
    out.clear();
    for (int i = 0; i < kModelCount; ++i) {
        const ModelBlob& mb = kModels[i];
        size_t o = 0;
        while (o + 8 <= mb.size) {
            uint32_t id = (uint32_t(mb.data[o]) << 24) | (uint32_t(mb.data[o + 1]) << 16) |
                          (uint32_t(mb.data[o + 2]) << 8) | mb.data[o + 3];
            uint16_t part = uint16_t((mb.data[o + 4] << 8) | mb.data[o + 5]);
            uint16_t len = uint16_t((mb.data[o + 6] << 8) | mb.data[o + 7]);
            o += 8;
            if (o + len > mb.size) break;
            std::pair<uint16_t, uint16_t> p{part, uint16_t(id >> 16)};
            if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
            o += len;
        }
    }
}

bool CreateCodeplug(const Codeplug& templ, const CreateOptions& opt, Codeplug& out,
                    std::string& err) {
    err.clear();
    if (templ.records.empty()) {
        err = "Template contains no records";
        return false;
    }
    if (opt.serial.empty() || opt.model.empty()) {
        err = "Serial and model are required";
        return false;
    }
    out = templ;
    if (!UpdateIdentity(out, opt.serial, opt.model)) {
        err = "Serial/model do not fit the template fields (use a same-model template)";
        return false;
    }
    if (!opt.flashcode.empty()) {
        std::string canonical;
        if (!SetFlashcodeMap(out, opt.flashcode, canonical, err)) return false;
    }
    return true;
}

static std::vector<uint8_t> PayloadOf(const Record& r) {
    if (r.raw.size() < 8) return {};
    return std::vector<uint8_t>(r.raw.begin() + 6, r.raw.end() - 2);
}
static std::string AsciiField(const std::vector<uint8_t>& p, size_t off, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && off + n < p.size() && p[off + n]) ++n;
    return std::string(reinterpret_cast<const char*>(p.data() + off), n);
}

double WordToMHz(uint32_t w) { return double(w) * 5.0 / 1e6; }
uint32_t MHzToWord(double mhz) { return static_cast<uint32_t>(std::llround(mhz * 1e6 / 5.0)); }

bool ParseZones(const Codeplug& cp, std::vector<ZoneView>& zones) {
    zones.clear();
    const Record* znames = FindRecord(cp, 0x000D, 0);
    if (!znames) return false;
    auto zn = PayloadOf(*znames);
    if (zn.size() < 8) return false;
    const int zoneCount = BE16(&zn[6]);

    for (int z = 0; z < zoneCount; ++z) {
        ZoneView zv;
        const size_t nameOff = 8 + size_t(z) * 17;
        if (nameOff + 16 <= zn.size()) zv.name = AsciiField(zn, nameOff, 16);

        std::vector<std::string> chanNames;
        const Record* zref = FindRecord(cp, 0x0FFE, uint16_t(z));
        if (zref) {
            auto rp = PayloadOf(*zref);
            if (rp.size() >= 12) {
                const uint16_t tag = BE16(&rp[8]);
                const uint16_t inst = BE16(&rp[10]);
                const Record* names = FindRecord(cp, tag, inst);
                if (names) {
                    auto np = PayloadOf(*names);
                    if (np.size() >= 6) {
                        const int n = BE16(&np[4]);
                        for (int i = 0; i < n; ++i) {
                            const size_t o = 6 + size_t(i) * 17;
                            if (o + 16 > np.size()) break;
                            chanNames.push_back(AsciiField(np, o, 16));
                        }
                    }
                }
            }
        }

        const Record* cref = FindRecord(cp, 0x0FFC, uint16_t(z));
        if (cref) {
            auto cp2 = PayloadOf(*cref);
            if (cp2.size() >= 12) {
                const uint16_t tag = BE16(&cp2[8]);
                const uint16_t sub = BE16(&cp2[10]);
                for (uint16_t page = sub; page <= uint16_t(sub + 1); ++page) {
                    const Record* pr = FindRecord(cp, tag, page);
                    if (!pr) continue;
                    auto pp = PayloadOf(*pr);
                    if (pp.size() < 6) continue;
                    const int n = BE16(&pp[4]);
                    for (int j = 0; j < n; ++j) {
                        const size_t o = 6 + size_t(j) * 35;
                        if (o + 35 > pp.size()) break;
                        if (BE16(&pp[o]) != 0x0FFB) continue;
                        ChannelView cv;
                        cv.rxWord = BE32(&pp[o + 12]);
                        cv.txWord = BE32(&pp[o + 16]);
                        cv.altWord = BE32(&pp[o + 20]);
                        cv.squelchRx = pp[o + 24];
                        cv.squelchTx = pp[o + 26];
                        cv.flags = pp[o + 27];
                        zv.channels.push_back(cv);
                    }
                }
            }
        }

        for (size_t i = 0; i < zv.channels.size() && i < chanNames.size(); ++i)
            zv.channels[i].name = chanNames[i];
        zones.push_back(std::move(zv));
    }
    return true;
}

bool SetZoneName(Codeplug& cp, int zone1, const std::string& name) {
    Record* r = FindRecord(cp, 0x000D, 0);
    if (!r) return false;
    auto p = PayloadOf(*r);
    if (p.size() < 8) return false;
    const int count = BE16(&p[6]);
    if (zone1 < 1 || zone1 > count) return false;
    const size_t off = 8 + size_t(zone1 - 1) * 17;
    if (off + 17 > p.size()) return false;
    memset(p.data() + off, 0, 17);
    memcpy(p.data() + off, name.data(), std::min<size_t>(name.size(), 16));
    return SetItemPayload(cp, 0x000D, 0, p);
}

bool SetChannelName(Codeplug& cp, int zone1, int chan1, const std::string& name) {
    const Record* zref = FindRecord(cp, 0x0FFE, uint16_t(zone1 - 1));
    if (!zref) return false;
    auto rp = PayloadOf(*zref);
    if (rp.size() < 12) return false;
    const uint16_t tag = BE16(&rp[8]);
    const uint16_t inst = BE16(&rp[10]);
    Record* names = FindRecord(cp, tag, inst);
    if (!names) return false;
    auto p = PayloadOf(*names);
    if (p.size() < 6) return false;
    const int count = BE16(&p[4]);
    if (chan1 < 1 || chan1 > count) return false;
    const size_t off = 6 + size_t(chan1 - 1) * 17;
    if (off + 17 > p.size()) return false;
    memset(p.data() + off, 0, 17);
    memcpy(p.data() + off, name.data(), std::min<size_t>(name.size(), 16));
    return SetItemPayload(cp, tag, inst, p);
}

bool SetChannelFreq(Codeplug& cp, int zone1, int chan1, double rxMHz, double txMHz) {
    const Record* cref = FindRecord(cp, 0x0FFC, uint16_t(zone1 - 1));
    if (!cref) return false;
    auto rp = PayloadOf(*cref);
    if (rp.size() < 12) return false;
    const uint16_t tag = BE16(&rp[8]);
    const uint16_t sub = BE16(&rp[10]);

    int target = chan1 - 1;
    for (uint16_t page = sub; page <= uint16_t(sub + 1); ++page) {
        Record* pr = FindRecord(cp, tag, page);
        if (!pr) continue;
        auto p = PayloadOf(*pr);
        if (p.size() < 6) continue;
        const int n = BE16(&p[4]);
        if (target >= n) {
            target -= n;
            continue;
        }
        const size_t o = 6 + size_t(target) * 35;
        if (o + 35 > p.size()) return false;
        const uint32_t rx = MHzToWord(rxMHz);
        const uint32_t tx = MHzToWord(txMHz);
        auto put32 = [&](size_t off, uint32_t v) {
            p[off] = uint8_t(v >> 24);
            p[off + 1] = uint8_t(v >> 16);
            p[off + 2] = uint8_t(v >> 8);
            p[off + 3] = uint8_t(v);
        };
        put32(o + 12, rx);
        put32(o + 16, tx);
        put32(o + 20, tx);
        p[o + 27] = (rx == tx) ? 0x80 : 0x40;
        return SetItemPayload(cp, tag, page, p);
    }
    return false;
}

bool MakeConventional(Codeplug& cp, std::string& err) {
    err.clear();
    std::vector<ZoneView> zones;
    if (!ParseZones(cp, zones)) { err = "No zone table"; return false; }
    int n = 0;
    while (n < (int)zones.size() && !zones[n].channels.empty()) ++n;
    if (n == 0) { err = "No conventional (channel-bearing) leading zones"; return false; }

    Record* zn = FindRecord(cp, 0x000D, 0);
    if (!zn) { err = "No zone-name record"; return false; }
    auto zp = PayloadOf(*zn);
    if (zp.size() < 8) { err = "Bad zone-name record"; return false; }
    std::vector<uint8_t> np(8, 0);
    std::copy(zp.begin(), zp.begin() + 8, np.begin());
    np[6] = uint8_t(n >> 8);
    np[7] = uint8_t(n);
    for (int i = 0; i < n; ++i) {
        size_t off = 8 + size_t(i) * 17;
        if (off + 17 > zp.size()) { err = "Zone name out of range"; return false; }
        np.insert(np.end(), zp.begin() + off, zp.begin() + off + 17);
    }
    return SetItemPayload(cp, 0x000D, 0, np);
}

bool MakeConventionalFromRef(Codeplug& out, const Codeplug& ref, const Codeplug& src,
                             std::string& err) {
    err.clear();
    std::vector<ZoneView> zones;
    if (!ParseZones(src, zones)) { err = "No zone table in source"; return false; }
    std::vector<int> keep;
    for (int i = 0; i < (int)zones.size(); ++i)
        if (!zones[i].channels.empty()) keep.push_back(i);
    if (keep.empty()) { err = "No conventional zones in source"; return false; }

    out = ref;
    out.header = src.header;
    out.keyBlob = src.keyBlob;
    out.seed = src.seed;
    out.encType = src.encType;

    auto isDyn = [](const Record& r) {
        const uint16_t t = uint16_t(r.id >> 16);
        return t == 0x000D || t == 0x0FA1 || t == 0x0FA6 || t == 0x0FFB || t == 0x0FFC || t == 0x0FFD || t == 0x0FFE ||
               t == 0x0FFF;
    };
    out.records.erase(std::remove_if(out.records.begin(), out.records.end(), isDyn),
                      out.records.end());

    auto addRec = [&](const Record& r) { out.records.push_back(r); };
    auto addPayload = [&](uint16_t tag, uint16_t inst, const std::vector<uint8_t>& p,
                          uint16_t type) {
        Record r;
        r.id = (uint32_t(tag) << 16) | inst;
        r.type = type;
        r.streamNum = 0;
        r.raw.resize(6 + p.size() + 2);
        r.raw[0] = uint8_t(tag >> 8); r.raw[1] = uint8_t(tag);
        r.raw[2] = uint8_t(inst >> 8); r.raw[3] = uint8_t(inst);
        const uint16_t len = uint16_t(p.size() + 2);
        r.raw[4] = uint8_t(len >> 8); r.raw[5] = uint8_t(len);
        if (!p.empty()) memcpy(r.raw.data() + 6, p.data(), p.size());
        const uint16_t crc = Crc16Ccitt(p.data(), p.size());
        r.raw[p.size() + 6] = uint8_t(crc >> 8);
        r.raw[p.size() + 7] = uint8_t(crc);
        r.checksum = crc;
        r.verified = true;
        out.records.push_back(std::move(r));
    };

    const Record* znRef = FindRecord(ref, 0x000D, 0);
    const Record* znSrc = FindRecord(src, 0x000D, 0);
    if (!znRef || !znSrc) { err = "missing zone-name record"; return false; }
    const auto zpR = PayloadOf(*znRef);
    const auto zpS = PayloadOf(*znSrc);
    if (zpR.size() < 8 || zpS.size() < 8) { err = "bad zone-name record"; return false; }
    std::vector<uint8_t> np(8);
    std::copy(zpR.begin(), zpR.begin() + 8, np.begin());
    np[6] = uint8_t(keep.size() >> 8);
    np[7] = uint8_t(keep.size());
    for (int old : keep) {
        const size_t off = 8 + size_t(old) * 17;
        if (off + 17 > zpS.size()) { err = "zone name out of range"; return false; }
        np.insert(np.end(), zpS.begin() + off, zpS.begin() + off + 17);
    }
    addPayload(0x000D, 0, np, znRef->type);

    std::set<uint32_t> copied;
    for (size_t i = 0; i < keep.size(); ++i) {
        const int old = keep[i];
        const Record* zr = FindRecord(src, 0x0FFE, uint16_t(old));
        const Record* cr = FindRecord(src, 0x0FFC, uint16_t(old));
        const Record* nr = FindRecord(src, 0x0FFF, uint16_t(old));
        if (!zr || !cr || !nr) { err = "missing zone/channel records"; return false; }
        auto zp = PayloadOf(*zr);
        if (zp.size() >= 12) {
            zp[10] = uint8_t(i >> 8);
            zp[11] = uint8_t(i);
        }
        const auto cp = PayloadOf(*cr);
        if (cp.size() < 12) { err = "bad zone channel table"; return false; }
        const uint16_t t = BE16(&cp[8]);
        const uint16_t sub = BE16(&cp[10]);
        const auto npSrc = PayloadOf(*nr);
        const int nameCount = npSrc.size() >= 6 ? BE16(&npSrc[4]) : 0;
        int base = 0;
        for (uint16_t page = sub; page <= uint16_t(sub + 1); ++page) {
            const Record* pr = FindRecord(src, t, page);
            if (!pr) { continue; }
            const auto pp = PayloadOf(*pr);
            const int n = pp.size() >= 6 ? BE16(&pp[4]) : 0;
            std::vector<uint8_t> table(6);
            if (pp.size() >= 6) std::copy(pp.begin(), pp.begin() + 6, table.begin());
            int kept = 0;
            for (int j = 0; j < n; ++j) {
                const size_t o = 6 + size_t(j) * 35;
                if (o + 35 > pp.size()) break;
                if (BE16(&pp[o]) != 0x0FFB) continue;
                table.insert(table.end(), pp.begin() + o, pp.begin() + o + 35);
                const uint16_t ei = BE16(&pp[o + 2]);
                const Record* er = FindRecord(src, 0x0FFB, ei);
                if (er && copied.insert(er->id).second) addRec(*er);
                ++kept;
            }
            table[4] = uint8_t(kept >> 8);
            table[5] = uint8_t(kept);
            addPayload(t, page, table, pr->type);
            base += n;
        }
        addPayload(0x0FFF, uint16_t(i), npSrc, nr->type);
        addPayload(0x0FFE, uint16_t(i), zp, zr->type);
        addPayload(0x0FFC, uint16_t(i), cp, cr->type);
    }

    if (Record* c = FindRecord(out, 0x000C, 0)) {
        auto p = PayloadOf(*c);
        if (!p.empty()) {
            p[0] = uint8_t(keep.size());
            SetItemPayload(out, 0x000C, 0, p);
        }
    }
    UpdateIdentity(out, src.header.serial, src.header.model);
    std::string canon, ferr;
    if (!SetFlashcodeMap(out, "600022150000", canon, ferr)) { err = ferr; return false; }
    return true;
}

std::string DumpCodeplug(const Codeplug& cp) {
    std::ostringstream os;
    os << VerifyCodeplug(cp);
    os << "--- records ---\n";
    for (const auto& r : cp.records) {
        char head[128];
        snprintf(head, sizeof(head), "0x%08X  strm=%-3d  size=%-4zu  type=0x%04X  csum=0x%04X\n",
                 r.id, r.streamNum, r.raw.size(), r.type, r.checksum);
        os << head;
        std::string text;
        for (size_t i = 6; i + 2 < r.raw.size(); ++i) {
            const uint8_t b = r.raw[i];
            text.push_back((b >= 32 && b < 127) ? char(b) : '.');
        }
        if (text.size() > 96) text.resize(96);
        if (text.find_first_not_of('.') != std::string::npos) os << "    |" << text << "|\n";
    }
    return os.str();
}

bool GetCodeplugPassword(const Codeplug& cp, CodeplugPassword& out) {
    out = {};
    const Record* r = nullptr;
    for (const auto& x : cp.records)
        if (x.id == 0x00510000u) { r = &x; break; }
    if (!r || r->raw.size() < 8) return false;
    out.present = true;
    const size_t plen = r->raw.size() - 8;
    if (plen == 0) return true;
    out.enabled = r->raw[6] != 0;
    for (size_t i = 1; i < plen; ++i) {
        const uint8_t c = r->raw[6 + i];
        if (!c) break;
        out.password.push_back(static_cast<char>(c));
    }
    return true;
}

bool SetCodeplugPassword(Codeplug& cp, bool enabled, const std::string& password) {
    Record* r = nullptr;
    for (auto& x : cp.records)
        if (x.id == 0x00510000u) { r = &x; break; }
    if (!r || r->raw.size() < 11) return false;
    const size_t plen = r->raw.size() - 8;
    for (size_t i = 0; i < plen; ++i) r->raw[6 + i] = 0;
    r->raw[6] = enabled ? 1 : 0;
    // The vendor reader zeroes the last two payload bytes; keep them reserved.
    const size_t maxLen = plen >= 3 ? plen - 3 : 0;
    for (size_t i = 0; i < password.size() && i < maxLen; ++i)
        r->raw[7 + i] = static_cast<uint8_t>(password[i]);
    const uint16_t crc = Crc16Ccitt(r->raw.data() + 6, plen);
    r->raw[6 + plen] = static_cast<uint8_t>(crc >> 8);
    r->raw[6 + plen + 1] = static_cast<uint8_t>(crc & 0xFF);
    r->checksum = crc;
    r->verified = true;
    return true;
}

}  // namespace opendepot
