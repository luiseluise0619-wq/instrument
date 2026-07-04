#pragma once

#include <JuceHeader.h>

/**
    Licensing for VocalChop Studio.

    Two kinds of key unlock the plugin:

    1. Gumroad keys (what buyers get automatically). Activation calls the
       Gumroad license-verify API once, which counts activations — a key is
       good for kMaxDevices machines. After that one online check the
       activation is stored locally (bound to this machine's ID) and the
       plugin never needs the network again.

    2. Offline "VCS-" keys (giveaways, press, no-internet fallback). The key
       is an RSA signature of the buyer's e-mail; verification is entirely
       offline against the public key below. No device counting.

    Until a key is entered the plugin runs as a DEMO: fully functional, but
    the output mutes for two seconds every minute.

    Honest threat model: this stops casual file-sharing, not crackers. The
    local licence file is machine-bound and tamper-signed, but anyone
    determined enough to patch the binary wins — as with every plugin.
*/
namespace vcs
{
struct Licensing
{
    // Gumroad product permalink (the bit after gumroad.com/l/...).
    // REPLACE once the product page exists, then rebuild.
    static constexpr const char* kGumroadPermalink = "vocalchopstudio";
    static constexpr int kMaxDevices = 3;

    // RSA public key for offline "VCS-" keys (e, n hex — juce::RSAKey format).
    static constexpr const char* kPublicKey =
        "10001,"
        "a93e24be4e0bc84531360cbdb5d2ba0db8ccab3102926bff321aeca7ade263c1"
        "6dafcc0e3efe03d66c2fc39df238c4971dcfe83f5579981a3b0b99276ce0712b"
        "d69314a8c652285d886ad111a1513e69d8c6e911fa3630bc0d6b45be2a91ae9d"
        "85dab54d11678a4be9fd1d13cbed461b9a88e89c80981644e182d31bb13d06a7";

    //==========================================================================
    static juce::String normEmail (const juce::String& e)
    {
        return e.trim().toLowerCase();
    }
    static juce::String normKey (const juce::String& k)
    {
        return k.removeCharacters (" \t\r\n-").trim();
    }

    /** True for our offline VIP keys ("VCS-...."). Call BEFORE normKey
        (normKey strips the dashes). */
    static bool looksLikeOfflineKey (const juce::String& raw)
    {
        return raw.trim().startsWithIgnoreCase ("VCS");
    }

    //==========================================================================
    // Offline "VCS-" keys: RSA signature of SHA256(e-mail).
    static bool verifyOfflineKey (const juce::String& emailIn, const juce::String& keyIn)
    {
        const auto email = normEmail (emailIn);
        auto key = normKey (keyIn);
        if (key.startsWithIgnoreCase ("VCS"))
            key = key.substring (3);
        if (email.isEmpty() || key.length() < 64)
            return false;

        juce::BigInteger sig;
        sig.parseString (key, 16);
        if (sig.isZero())
            return false;

        juce::RSAKey pub (kPublicKey);
        pub.applyToValue (sig);   // sig^e mod n

        const juce::SHA256 sha (email.toRawUTF8(), (size_t) email.getNumBytesAsUTF8());
        juce::BigInteger want;
        want.parseString (sha.toHexString().removeCharacters (" "), 16);

        return sig == want;
    }

    //==========================================================================
    // Gumroad keys: one online check, counted per device by Gumroad.
    struct OnlineResult
    {
        bool ok = false;
        juce::String message;   // human-readable, shown in the unlock panel
        juce::String email;     // buyer e-mail from the receipt (if any)
    };

    /** BLOCKING network call — run it on a background thread. */
    static OnlineResult activateOnline (const juce::String& keyIn)
    {
        OnlineResult r;
        const auto key = keyIn.trim();

        juce::URL url ("https://api.gumroad.com/v2/licenses/verify");
        url = url.withPOSTData ("product_permalink=" + juce::URL::addEscapeChars (kGumroadPermalink, true)
                                + "&license_key=" + juce::URL::addEscapeChars (key, true)
                                + "&increment_uses_count=true");

        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (8000));

        if (stream == nullptr)
        {
            r.message = "No internet connection. Check the network and retry "
                        "(activation needs to go online just this once).";
            return r;
        }

        const auto response = stream->readEntireStreamAsString();
        const auto json = juce::JSON::parse (response);

        if (! json.getProperty ("success", false))
        {
            r.message = "Key not recognised. Paste the license key exactly as "
                        "it appears on your Gumroad receipt.";
            return r;
        }

        const auto purchase = json.getProperty ("purchase", juce::var());
        if (purchase.getProperty ("refunded", false)
            || purchase.getProperty ("chargebacked", false))
        {
            r.message = "This purchase was refunded, so the key is no longer active.";
            return r;
        }

        const int uses = (int) json.getProperty ("uses", 0);
        if (uses > kMaxDevices)
        {
            r.message = "This key has already been activated on "
                        + juce::String (kMaxDevices) + " devices.";
            return r;
        }

        r.ok = true;
        r.email = purchase.getProperty ("email", juce::String()).toString();
        r.message = "Activated - thank you for supporting VocalChop Studio!";
        return r;
    }

    //==========================================================================
    // Local activation file: machine-bound + tamper-signed.
    static juce::String machineId()
    {
        return juce::SystemStats::getUniqueDeviceID();
    }

    static juce::String localSig (const juce::String& key, const juce::String& machine)
    {
        const auto material = normKey (key) + "|" + machine + "|vcs.rider.7f2";
        const juce::SHA256 sha (material.toRawUTF8(), (size_t) material.getNumBytesAsUTF8());
        return sha.toHexString().removeCharacters (" ");
    }

    static juce::File licenseFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("VocalChop Studio")
                   .getChildFile ("license.xml");
    }

    static bool saveActivation (const juce::String& email, const juce::String& key)
    {
        auto f = licenseFile();
        f.getParentDirectory().createDirectory();

        juce::XmlElement xml ("VCSLicense");
        xml.setAttribute ("email",   normEmail (email));
        xml.setAttribute ("key",     key.trim());
        xml.setAttribute ("machine", machineId());
        xml.setAttribute ("sig",     localSig (key, machineId()));
        return xml.writeTo (f);
    }

    /** True when a valid activation for THIS machine is on disk. */
    static bool loadActivation()
    {
        const auto f = licenseFile();
        if (! f.existsAsFile())
            return false;

        const auto xml = juce::parseXML (f);
        if (xml == nullptr || ! xml->hasTagName ("VCSLicense"))
            return false;

        const auto key     = xml->getStringAttribute ("key");
        const auto machine = xml->getStringAttribute ("machine");
        if (machine != machineId()
            || xml->getStringAttribute ("sig") != localSig (key, machine))
            return false;

        // Offline keys re-verify cryptographically; Gumroad keys were
        // verified online at activation and are trusted from the local sig.
        if (looksLikeOfflineKey (key))
            return verifyOfflineKey (xml->getStringAttribute ("email"), key);

        return key.isNotEmpty();
    }
};
} // namespace vcs
