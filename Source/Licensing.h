#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
    Slyce licensing client. The authority to accept or issue a key lives in
    the Vercel API, never in this binary. This makes revoked keys, Gumroad
    refunds, seller-issued keys, and device limits enforceable in one place.
*/
namespace vcs
{
struct Licensing
{
   #ifndef SLYCE_LICENSE_API_URL
    static constexpr const char* kLicenseApiUrl =
        "https://instrument-nu.vercel.app/api/license/activate";
   #else
    static constexpr const char* kLicenseApiUrl = SLYCE_LICENSE_API_URL;
   #endif

    static juce::String normEmail (const juce::String& email)
    {
        return email.trim().toLowerCase();
    }

    /** Removes whitespace and invisible characters introduced by copy/paste. */
    static juce::String stripInvisible (const juce::String& key)
    {
        juce::String out;
        for (auto c : key)
        {
            const bool drop = c <= 0x20 || c == 0x7f
                           || (c >= 0x80 && c <= 0x9f) || c == 0xa0
                           || (c >= 0x2000 && c <= 0x200f)
                           || (c >= 0x2028 && c <= 0x202e)
                           || c == 0x205f || c == 0x2060 || c == 0x3000
                           || c == 0xfeff;
            if (! drop)
                out += juce::String::charToString (c);
        }
        return out;
    }

    /** Do not transmit JUCE's raw device ID. The server only receives this
        one-way hash, which it uses to enforce the device limit. */
    static juce::String machineHash()
    {
        const auto device = juce::SystemStats::getUniqueDeviceID();
        const juce::SHA256 sha (device.toRawUTF8(), (size_t) device.getNumBytesAsUTF8());
        return sha.toHexString().removeCharacters (" ");
    }

    struct OnlineResult
    {
        bool ok = false;
        juce::String message;
        juce::String email;
    };

    /** Blocking network call. Call only from a worker thread. */
    static OnlineResult activateOnline (const juce::String& keyIn,
                                        const juce::String& emailIn,
                                        std::atomic<bool>* cancelFlag = nullptr)
    {
        OnlineResult result;
        const auto key = stripInvisible (keyIn);
        const auto email = normEmail (emailIn);
        if (key.isEmpty() || email.isEmpty())
        {
            result.message = "Enter the Gumroad purchase e-mail and license key.";
            return result;
        }

        juce::URL url (kLicenseApiUrl);
        url = url.withPOSTData ("key=" + juce::URL::addEscapeChars (key, true)
                                + "&email=" + juce::URL::addEscapeChars (email, true)
                                + "&machine=" + juce::URL::addEscapeChars (machineHash(), true));

        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (8000)
                .withProgressCallback ([cancelFlag] (int, int)
                {
                    return cancelFlag == nullptr || ! cancelFlag->load();
                }));

        if (stream == nullptr)
        {
            result.message = "Could not reach the Slyce license server. Check your connection and retry.";
            return result;
        }

        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        if (! json.getProperty ("ok", false))
        {
            result.message = json.getProperty ("message", "License verification failed.").toString();
            return result;
        }

        result.ok = true;
        result.email = json.getProperty ("email", email).toString();
        result.message = json.getProperty ("message", "License verified.").toString();
        return result;
    }
};
} // namespace vcs
