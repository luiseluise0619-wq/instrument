#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
    Licensing for Slyce (formerly VocalChop Studio).

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
    the output mutes for two seconds every thirty, and the
    session state is never written, so a project cannot be reopened intact.

    Honest threat model: this stops casual file-sharing, not crackers. The
    local licence file is machine-bound and tamper-signed, but anyone
    determined enough to patch the binary wins — as with every plugin.
*/
namespace vcs
{
struct Licensing
{
    // Gumroad product ID (shown next to the License key block on the
    // product's Content page). IDs are stable even if the page URL changes.
    static constexpr const char* kGumroadProductId = "XfHXdgZ24Sjh9vU9PkzrZA==";
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
    /** Removes what a PASTE adds, and nothing else.

        Whitespace of every kind, plus the zero-width and directional marks
        that web pages, e-mail clients and chat apps slip into copied text:
        U+00A0, U+200B-U+200F, U+2028/29, the en/em/thin spaces, the word
        joiner and a leading BOM. Everything printable survives - crucially
        the DASHES, because a Gumroad key is sent back to Gumroad with its
        dashes intact and normKey (which strips them) would corrupt it. */
    static juce::String stripInvisible (const juce::String& k)
    {
        juce::String out;
        for (auto c : k)
        {
            const bool drop =
                   c <= 0x20                       // space and the C0 controls
                || c == 0x7f                       // DEL
                || (c >= 0x80 && c <= 0x9f)        // C1 controls
                || c == 0xa0                       // NO-BREAK SPACE
                || (c >= 0x2000 && c <= 0x200f)    // en/em/thin spaces, ZWSP, LRM/RLM
                || (c >= 0x2028 && c <= 0x202e)    // line/para separators, bidi embeds
                || c == 0x205f || c == 0x2060      // medium math space, word joiner
                || c == 0x3000                     // ideographic space
                || c == 0xfeff;                    // BOM / zero-width no-break space
            if (! drop)
                out += juce::String::charToString (c);
        }
        return out;
    }

    /** Keeps letters and digits, drops everything else.

        This used to remove only " \t\r\n-", which is the ASCII half of the
        problem. A licence key reaches the plugin by being copied out of a web
        page, an e-mail or a chat window, and all three routinely hand over
        U+00A0 (non-breaking space) or U+200B (zero-width space) instead of, or
        as well as, a plain space. Those characters survived the old filter, so
        the key failed to parse and the buyer was told their key was not
        recognised - with nothing on screen to suggest that an invisible
        character was the reason.

        Keeping only alphanumerics is right for both formats: ours is hex with
        dashes, Gumroad's is alphanumeric groups with dashes. */
    static juce::String normKey (const juce::String& k)
    {
        juce::String out;
        for (auto c : stripInvisible (k))
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                out += juce::String::charToString (c);
        return out;
    }

    /** True for our offline VIP keys ("VCS-....").

        Normalises FIRST. Testing raw.trim() meant a key that began with a
        non-breaking space was not recognised as one of ours and was sent to
        Gumroad's server instead, which of course rejected it - the same
        invisible character, failing a second way. */
    static bool looksLikeOfflineKey (const juce::String& raw)
    {
        return normKey (raw).startsWithIgnoreCase ("VCS");
    }

    //==========================================================================
    // Offline "VCS-" keys: RSA signature of SHA256(e-mail).
    static bool verifyOfflineKey (const juce::String& emailIn, const juce::String& keyIn)
    {
        const auto email = normEmail (emailIn);
        auto key = normKey (keyIn);          // invisible characters gone
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

    /** BLOCKING network call — run it on a background thread. A non-null
        cancelFlag aborts the connection attempt (checked via the progress
        callback) so a closing editor never waits out the full timeout. */
    static OnlineResult activateOnline (const juce::String& keyIn,
                                        std::atomic<bool>* cancelFlag = nullptr)
    {
        OnlineResult r;
        // stripInvisible, NOT trim. trim() only removes characters at or below
        // ' ', so a key pasted out of a receipt page or a chat window carries
        // its U+00A0 all the way into the POST body, where Gumroad compares it
        // against the real key and says no. That is a PAYING customer being
        // told their key is invalid, and the first fix for this missed it -
        // normKey cannot be used here because it strips the dashes Gumroad
        // needs, so the two normalisations have to stay separate.
        const auto key = stripInvisible (keyIn);

        juce::URL url ("https://api.gumroad.com/v2/licenses/verify");
        url = url.withPOSTData ("product_id=" + juce::URL::addEscapeChars (kGumroadProductId, true)
                                + "&license_key=" + juce::URL::addEscapeChars (key, true)
                                + "&increment_uses_count=true");

        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (8000)
                .withProgressCallback ([cancelFlag] (int, int)
                {
                    return cancelFlag == nullptr || ! cancelFlag->load();
                }));

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
        r.message = "Activated - thank you for supporting Slyce!";
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
                   .getChildFile ("Slyce")
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
