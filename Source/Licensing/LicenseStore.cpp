#include "LicenseStore.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincred.h>
#elif JUCE_MAC
 #include <Security/Security.h>
#endif

namespace drumbreaker::licensing
{

LicenseStore::LicenseStore (juce::String scope)
    : target ("HiddenAudio.DrumBreaker." + juce::String (productId) + "."
              + juce::String (variantId) + "." + scope) {}

#if JUCE_MAC
static CFMutableDictionaryRef makeQuery (const juce::String& target)
{
    auto dictionary = CFDictionaryCreateMutable (kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto service = CFStringCreateWithCString (kCFAllocatorDefault, target.toRawUTF8(), kCFStringEncodingUTF8);
    CFDictionarySetValue (dictionary, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue (dictionary, kSecAttrService, service);
    CFDictionarySetValue (dictionary, kSecAttrAccount, CFSTR ("activation"));
    CFDictionarySetValue (dictionary, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
    CFRelease (service);
    return dictionary;
}
#endif

juce::Result LicenseStore::load (StoredActivation& record, bool& found) const
{
    record = {};
    found = false;
    juce::String json;
   #if JUCE_WINDOWS
    PCREDENTIALW credential = nullptr;
    if (! CredReadW (target.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &credential))
        return GetLastError() == ERROR_NOT_FOUND ? juce::Result::ok()
            : juce::Result::fail ("Windows secure credential storage could not be read.");
    if (credential->CredentialBlobSize == 0 || credential->CredentialBlobSize > CRED_MAX_CREDENTIAL_BLOB_SIZE)
    {
        CredFree (credential);
        return juce::Result::fail ("The saved activation record has an invalid size.");
    }
    json = juce::String::fromUTF8 ((const char*) credential->CredentialBlob, (int) credential->CredentialBlobSize);
    SecureZeroMemory (credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree (credential);
   #elif JUCE_MAC
    auto query = makeQuery (target);
    CFDictionarySetValue (query, kSecReturnData, kCFBooleanTrue);
    CFTypeRef output = nullptr;
    const auto result = SecItemCopyMatching (query, &output);
    CFRelease (query);
    if (result == errSecItemNotFound)
        return juce::Result::ok();
    if (result != errSecSuccess || output == nullptr)
        return juce::Result::fail ("macOS Keychain could not read the saved activation.");
    if (CFGetTypeID (output) != CFDataGetTypeID() || CFDataGetLength ((CFDataRef) output) > 2560)
    {
        CFRelease (output);
        return juce::Result::fail ("The saved activation record has an invalid size.");
    }
    json = juce::String::fromUTF8 ((const char*) CFDataGetBytePtr ((CFDataRef) output),
                                  (int) CFDataGetLength ((CFDataRef) output));
    CFRelease (output);
   #else
    return juce::Result::fail ("Protected licensing storage is unsupported on this platform.");
   #endif
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed() || ! parsed.isObject())
        return juce::Result::fail ("The saved activation record is damaged.");
    const auto version = parsed.getProperty ("version", {});
    const auto product = parsed.getProperty ("product", {});
    const auto variant = parsed.getProperty ("variant", {});
    const auto key = parsed.getProperty ("key", {});
    const auto device = parsed.getProperty ("device", {});
    const auto instance = parsed.getProperty ("instance", {});
    if (! version.isInt() || (int) version != 1 || ! product.isInt() || (int) product != productId
        || ! variant.isInt() || (int) variant != variantId
        || ! key.isString() || key.toString().trim().isEmpty() || key.toString().length() > 512
        || ! device.isString() || device.toString().isEmpty() || device.toString().length() > 256
        || ! instance.isString() || instance.toString().isEmpty() || instance.toString().length() > 256)
        return juce::Result::fail ("The saved activation record is invalid or belongs to another product.");
    record = { key.toString(), device.toString(), instance.toString() };
    found = true;
    return juce::Result::ok();
}

juce::Result LicenseStore::save (const StoredActivation& record) const
{
    if (record.key.trim().isEmpty() || record.key.length() > 512 || record.device.isEmpty()
        || record.device.length() > 256 || record.instance.isEmpty() || record.instance.length() > 256)
        return juce::Result::fail ("Incomplete activation information cannot be saved.");
    auto* object = new juce::DynamicObject();
    object->setProperty ("version", 1);
    object->setProperty ("product", productId);
    object->setProperty ("variant", variantId);
    object->setProperty ("key", record.key);
    object->setProperty ("device", record.device);
    object->setProperty ("instance", record.instance);
    const auto json = juce::JSON::toString (juce::var (object), true);
    const auto bytes = json.getNumBytesAsUTF8();
    if (bytes == 0 || bytes > 2560)
        return juce::Result::fail ("The activation record is too large for protected storage.");
   #if JUCE_WINDOWS
    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR> (target.toWideCharPointer());
    credential.UserName = const_cast<LPWSTR> (L"DrumBreaker activation");
    credential.CredentialBlob = (LPBYTE) json.toRawUTF8();
    credential.CredentialBlobSize = (DWORD) bytes;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW (&credential, 0) ? juce::Result::ok()
        : juce::Result::fail ("Windows secure credential storage could not save this activation.");
   #elif JUCE_MAC
    auto query = makeQuery (target);
    auto data = CFDataCreate (kCFAllocatorDefault, (const UInt8*) json.toRawUTF8(), (CFIndex) bytes);
    auto changes = CFDictionaryCreateMutable (kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue (changes, kSecValueData, data);
    auto result = SecItemUpdate (query, changes);
    if (result == errSecItemNotFound)
    {
        CFDictionarySetValue (query, kSecValueData, data);
        result = SecItemAdd (query, nullptr);
    }
    CFRelease (changes);
    CFRelease (data);
    CFRelease (query);
    return result == errSecSuccess ? juce::Result::ok()
        : juce::Result::fail ("macOS Keychain could not save this activation.");
   #else
    return juce::Result::fail ("Protected licensing storage is unsupported on this platform.");
   #endif
}

juce::Result LicenseStore::remove() const
{
   #if JUCE_WINDOWS
    if (CredDeleteW (target.toWideCharPointer(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
        return juce::Result::ok();
    return juce::Result::fail ("Windows secure credential storage could not remove the test record.");
   #elif JUCE_MAC
    auto query = makeQuery (target);
    const auto result = SecItemDelete (query);
    CFRelease (query);
    return result == errSecSuccess || result == errSecItemNotFound ? juce::Result::ok()
        : juce::Result::fail ("macOS Keychain could not remove the test record.");
   #else
    return juce::Result::fail ("Protected licensing storage is unsupported on this platform.");
   #endif
}

} // namespace drumbreaker::licensing