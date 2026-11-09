#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opendepot {

struct HeaderInfo {
    std::array<uint8_t, 800> raw{};
    std::string version;
    std::string model;
    std::string serial;
    std::array<uint8_t, 16> guid{};
    uint8_t flag = 0;
    int encType = 0;
    int adkType = 0;
};

struct Record {
    uint32_t id = 0;
    uint16_t type = 0;
    uint16_t checksum = 0;
    int streamNum = 0;
    std::vector<uint8_t> raw;
    bool verified = false;
};

struct Codeplug {
    HeaderInfo header;
    std::vector<Record> records;
    std::vector<uint8_t> mapPlain;
    std::vector<uint8_t> keyBlob;
    std::vector<uint8_t> seed;
    int encType = 0;
    std::string error;
};

bool LoadCodeplug(const std::wstring& path, Codeplug& cp);
bool SaveCodeplug(const std::wstring& path, const Codeplug& cp, std::string& err);
std::string VerifyCodeplug(const Codeplug& cp);
std::string DumpCodeplug(const Codeplug& cp);
bool UpdateIdentity(Codeplug& cp, const std::string& serial, const std::string& model);
bool ReadIdentityFromCodeplug(const Codeplug& cp, std::string& serial, std::string& model);
bool ValidateRadioCodeplug(const Codeplug& cp, std::string& err);

// Codeplug read/write passcode lives in record 0x00510000 (partition 0x80):
// payload[0] = enabled, payload[1..] = ASCII password (zero padded).
struct CodeplugPassword {
    bool present = false;
    bool enabled = false;
    std::string password;
};
bool GetCodeplugPassword(const Codeplug& cp, CodeplugPassword& out);
bool SetCodeplugPassword(Codeplug& cp, bool enabled, const std::string& password);

// Service report (model/serial/flashcode+features/passcode/zones; banner = AT&V text).
std::string ServiceReportText(const Codeplug& cp, const std::string& banner);
std::string ServiceReportJson(const Codeplug& cp, const std::string& banner);

// Codeplug doctor: verify/repair checksums, CRC, identity.
struct DoctorResult {
    std::vector<std::string> issues;
    std::vector<std::string> fixes;
    bool healthy() const { return issues.empty(); }
};
bool DoctorCodeplug(Codeplug& cp, DoctorResult& result, bool repair);

// Human-readable diff of two codeplugs.
std::string DiffCodeplugsText(const Codeplug& a, const Codeplug& b);
std::string DiffCodeplugsJson(const Codeplug& a, const Codeplug& b);

struct FeatureInfo {
    int nibble;
    int bit;
    const char* name;
};
const std::vector<FeatureInfo>& FeatureMatrix();
std::string GetFlashcode(const Codeplug& cp);
std::string CanonicalFlashcode(const std::string& payload12);
std::string FlashcodeToNibbles(const std::string& flashcode);
bool SetFlashcode(Codeplug& cp, const std::string& payloadOrFull, std::string& canonical,
                  std::string& err);
bool SetFlashcodeMap(Codeplug& cp, const std::string& payloadOrFull, std::string& canonical,
                     std::string& err);
std::vector<std::string> EnabledFeatures(const std::string& canonicalFlashcode);

struct ModelInfo {
    const char* band;
    const char* radio;
    const char* type;
    const char* model;
};
const std::vector<std::string>& BandOptions();
const std::vector<std::string>& RadioOptions();
const std::vector<std::string>& TypeOptions();
const std::vector<ModelInfo>& ModelCatalog();
std::vector<std::string> PackagesForModel(const std::string& model);
std::vector<std::string> AddonsForModel(const std::string& model);
std::string OptionLabel(const std::string& code);

struct FlashcodeOption {
    int nibble = 0;
    int bit = 0;
    std::string code;
    std::string label;
};
struct FlashcodeCodes {
    std::string key;
    std::vector<std::string> codes;
};
struct FlashcodePlatform {
    std::string name;
    std::vector<FlashcodeOption> options;
    std::vector<FlashcodeCodes> packages;
    std::vector<FlashcodeCodes> requires;
};
const std::vector<FlashcodePlatform>& FlashcodePlatforms();
const FlashcodePlatform* FindFlashcodePlatform(const std::string& name);
std::string PlatformForRadio(const std::string& radio);
std::string BuildFlashcode(const std::string& platform, const std::string& baseFlashcode,
                           const std::vector<std::string>& selectedCodes);
std::vector<std::string> DecodeFlashcode(const std::string& platform,
                                         const std::string& flashcode);

struct CreateOptions {
    std::string band = "UHF";
    std::string radio = "XTS5000";
    std::string type = "Model III";
    std::string model = "H46UCF9PW6BN";
    std::string serial;
    std::string flashcode;
    std::vector<std::string> packages;
    std::vector<std::string> addons;
};
bool BuiltinTemplate(const std::string& model, Codeplug& out, std::string& err);
void BuiltinTagList(std::vector<std::pair<uint16_t, uint16_t>>& out);
std::string BuiltinFirstModel();
struct ReadRequest {
    uint16_t part = 0;
    uint16_t tag = 0;
    uint16_t inst = 0;
    uint16_t len = 0;
};
void BuiltinReadRequests(std::vector<ReadRequest>& out);
bool CreateCodeplug(const Codeplug& templ, const CreateOptions& opt, Codeplug& out,
                    std::string& err);
bool MakeConventional(Codeplug& cp, std::string& err);
bool MakeConventionalFromRef(Codeplug& out, const Codeplug& ref, const Codeplug& src,
                             std::string& err);

struct ChannelView {
    std::string name;
    uint32_t rxWord = 0;
    uint32_t txWord = 0;
    uint32_t altWord = 0;
    uint8_t squelchRx = 0;
    uint8_t squelchTx = 0;
    uint8_t flags = 0;
};
struct ZoneView {
    std::string name;
    std::vector<ChannelView> channels;
};
bool ParseZones(const Codeplug& cp, std::vector<ZoneView>& zones);
bool SetZoneName(Codeplug& cp, int zone1, const std::string& name);
bool SetChannelName(Codeplug& cp, int zone1, int chan1, const std::string& name);
bool SetChannelFreq(Codeplug& cp, int zone1, int chan1, double rxMHz, double txMHz);
double WordToMHz(uint32_t word);
uint32_t MHzToWord(double mhz);

uint16_t Crc16Ccitt(const uint8_t* p, size_t n);

bool AdkCustomDecrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
bool AdkCustomEncrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

bool Rc4Decrypt(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& in,
                std::vector<uint8_t>& out);
bool Rc4Encrypt(const std::vector<uint8_t>& seed, const std::vector<uint8_t>& in,
                std::vector<uint8_t>& out);

}  // namespace opendepot

