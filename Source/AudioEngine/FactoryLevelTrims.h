#pragma once
#include <array>
// Fixed dB trims derived by tools/calibrate_factory.py. Partial correction
// within each family; velocities, transient shape and relative voices remain.
// Reference renders are finite-signal tests, not subjective listening approval.
namespace slyce::factory {
inline constexpr std::array<float,469> levelTrimsDb {{
    0.00f, // 0: Init Synth
    -0.81f, // 1: Neon Bass
    -0.42f, // 2: Sub 808
    0.64f, // 3: Reese Bass
    -2.10f, // 4: Wobble Growl
    2.50f, // 5: Pluck Bass
    -0.01f, // 6: Analog Warm
    2.50f, // 7: FM Knock
    -0.20f, // 8: Moog Bass
    2.09f, // 9: Future Bass
    -0.01f, // 10: Deep House
    0.72f, // 11: Neuro Bass
    -1.47f, // 12: Garage Sub
    2.50f, // 13: Slap Funk
    0.24f, // 14: Dark Reese
    2.32f, // 15: Rubber Bass
    2.50f, // 16: Acid Bass
    0.62f, // 17: Gnarl Bass
    -0.89f, // 18: Memphis 808
    -2.07f, // 19: Trap Knock
    2.38f, // 20: Outrun Bass
    2.47f, // 21: Octave Disco
    2.50f, // 22: Psy Stomp
    0.11f, // 23: Dusty Bass
    1.36f, // 24: Lately Bass
    0.96f, // 25: Talk Bass
    -3.19f, // 26: Pedal Organ
    1.44f, // 27: Rust Bass
    -1.76f, // 28: Liquid Sub
    1.20f, // 29: Syn Jazz Bass
    -2.09f, // 30: Rage 808
    -0.79f, // 31: Growl 808
    1.12f, // 32: Bounce Bass
    -0.69f, // 33: Seoul Bass
    2.50f, // 34: Donk Bass
    0.25f, // 35: Hoover Bass
    0.29f, // 36: UK Bass
    -2.28f, // 37: Growl Sub
    0.99f, // 38: Metal Bass
    -1.96f, // 39: Drift Phonk
    -1.04f, // 40: Jersey Boom
    -1.69f, // 41: Hyper Sub
    0.00f, // 42: Afro Log Bass
    -1.38f, // 43: Reggaeton Sub
    -1.65f, // 44: Club Rumble
    -1.95f, // 45: Drill Slide
    -0.93f, // 46: Sidechain Bass
    -0.00f, // 47: Reese Growl
    -2.14f, // 48: Silicon Sub
    0.36f, // 49: Glass Bass
    -0.91f, // 50: Drum Kit
    -0.93f, // 51: Kick 808
    -0.22f, // 52: Kick Punch
    -0.40f, // 53: Snare 808
    0.00f, // 54: Snare Tight
    -0.33f, // 55: Clap
    0.90f, // 56: Hat Closed
    -1.23f, // 57: Hat Open
    0.90f, // 58: Rim Perc
    -0.74f, // 59: Tom Low
    -0.35f, // 60: Tom High
    0.06f, // 61: Perc 909
    0.17f, // 62: Cowbell 808
    0.05f, // 63: Shaker
    -0.22f, // 64: Tambo Jingle
    0.04f, // 65: Syn Conga
    0.95f, // 66: Clave
    1.70f, // 67: Woodblock
    0.54f, // 68: Finger Snap
    1.62f, // 69: Hat Tight
    2.50f, // 70: Hat Roll
    0.76f, // 71: Hat Pedal
    -1.53f, // 72: Hat Sizzle
    -1.61f, // 73: Hat Trap Open
    -2.12f, // 74: Ride Ping
    -2.57f, // 75: Crash Splash
    0.00f, // 76: Tambourine
    2.05f, // 77: Perc Click
    0.92f, // 78: Rim Snap
    0.15f, // 79: Amapiano Shaker
    -0.38f, // 80: Afro Conga
    0.42f, // 81: Reggaeton Perc
    -0.76f, // 82: Festival Kick
    -0.17f, // 83: Trap Snare
    0.04f, // 84: Drum Machine
    -1.02f, // 85: Big Drums
    -0.45f, // 86: Drum Fill
    -0.73f, // 87: Vox Choir
    0.01f, // 88: Vox Ahh
    -0.62f, // 89: Vox Ooh
    0.28f, // 90: Vox Lead
    2.50f, // 91: Vox Pluck
    2.50f, // 92: Vox Stab
    2.50f, // 93: Chop Vox
    -2.33f, // 94: Robot Vox
    -1.49f, // 95: Talkbox Vox
    -0.80f, // 96: Vox Hum
    -0.35f, // 97: Whisper Air
    -0.51f, // 98: Angel Choir
    2.50f, // 99: Beatbox Kick
    2.50f, // 100: Beatbox Snare
    2.50f, // 101: Beatbox Hat
    -0.10f, // 102: Diva Vox
    -0.75f, // 103: Deep Choir
    -0.19f, // 104: Vox Ahh Wide
    -1.33f, // 105: Vox Hum Ooh
    -0.77f, // 106: Vox Chant Low
    0.52f, // 107: Vox Doo Choir
    -0.38f, // 108: Vox Siren Air
    1.83f, // 109: Reverse Vocal
    1.31f, // 110: Vocal Adlib
    0.00f, // 111: Vocal Harmony
    1.18f, // 112: Vocal Loop
    0.55f, // 113: Formant Vocal
    -0.38f, // 114: Neon 84 Lead
    -1.02f, // 115: Trap Flute
    -0.05f, // 116: Moody Keys
    2.00f, // 117: Isla Pluck
    -1.20f, // 118: Tropic Flute
    -1.42f, // 119: Whisper Bass
    -1.35f, // 120: Chart 808
    -0.13f, // 121: Emo Lead
    0.41f, // 122: Funk Brass
    -0.00f, // 123: Boogie Bass
    1.45f, // 124: Sunny Pluck
    -0.32f, // 125: Stadium Lead
    0.00f, // 126: Wave Keys
    -0.43f, // 127: Retro Pop Poly
    -0.15f, // 128: Anthem Brass
    -0.69f, // 129: Smooth EP
    -0.61f, // 130: Hit Pad
    2.23f, // 131: K-Pop Pluck
    1.41f, // 132: Hook Marimba
    1.09f, // 133: Log Drum
    0.12f, // 134: Future Chords
    0.84f, // 135: Cloud Bell
    1.41f, // 136: Phonk Cowbell
    1.10f, // 137: Piano Stab
    1.16f, // 138: Dancehall Pluck
    -1.37f, // 139: Drill 808
    1.06f, // 140: Slap House
    2.30f, // 141: Sped-Up Pluck
    0.25f, // 142: Tape Rewind
    1.81f, // 143: Laser Zap
    -0.47f, // 144: Riser Sweep
    -0.17f, // 145: Downlifter
    1.22f, // 146: Vocal Chop Hit
    0.32f, // 147: Supersaw Lead
    -1.79f, // 148: Retro Lead
    -0.16f, // 149: Acid Lead
    -1.48f, // 150: Chip Lead
    0.13f, // 151: Scream Lead
    -2.08f, // 152: PWM Lead
    -0.49f, // 153: Velvet Lead
    -2.22f, // 154: Hard Lead
    -2.24f, // 155: Italo Lead
    -1.04f, // 156: Flute Lead
    -0.02f, // 157: Rude Lead
    0.75f, // 158: Dream Lead
    -0.14f, // 159: Wire Lead
    0.22f, // 160: Midnight Lead
    2.50f, // 161: Idol Pluck
    -1.72f, // 162: Funk Worm
    0.00f, // 163: Grime Hoover
    -1.69f, // 164: Haze Lead
    -0.21f, // 165: Glide Solo
    0.07f, // 166: Solo Brass
    -0.45f, // 167: NES Round
    1.90f, // 168: Goa Lead
    -0.36f, // 169: Mainstage
    -1.85f, // 170: Silk Lead
    0.16f, // 171: Epic Horn
    1.24f, // 172: Vowel Lead
    2.45f, // 173: Rage Bell
    0.90f, // 174: Rage Lead
    2.50f, // 175: Laser Lead
    -1.07f, // 176: Whistle Lead
    0.11f, // 177: Saw Stack
    0.13f, // 178: Festival Lead
    -1.89f, // 179: Neon Lead
    -1.10f, // 180: Phonk Whistle
    0.91f, // 181: Drill Flute
    0.31f, // 182: Rave Hoover
    2.50f, // 183: Afro Marimba
    1.59f, // 184: Amapiano Lead
    0.23f, // 185: K-Pop Saw
    -0.22f, // 186: Hyperpop Squeak
    2.50f, // 187: Drill Bell Lead
    -0.39f, // 188: Analog Poly
    -3.34f, // 189: PWM Strings
    -0.98f, // 190: Hoover
    -2.38f, // 191: 80s Poly
    0.15f, // 192: FM Digital
    0.58f, // 193: Trance Saw
    2.50f, // 194: Rave Stab
    2.50f, // 195: Techno Stab
    2.50f, // 196: French Chord
    -1.45f, // 197: Ambient Drone
    0.13f, // 198: Glass Keys
    2.50f, // 199: Ice Pluck
    -3.60f, // 200: Soft Organ
    -0.61f, // 201: Cold Strings
    -0.32f, // 202: Lo-Fi Keys
    2.50f, // 203: Modular Blip
    -0.66f, // 204: Squelch 303
    2.50f, // 205: Bigroom Stab
    0.82f, // 206: 2-Step Chord
    -2.41f, // 207: 8-Bit Poly
    -0.24f, // 208: Mall Keys
    -0.88f, // 209: CS-80 Brass
    2.08f, // 210: Gabber Stab
    2.50f, // 211: Tech Blip
    0.13f, // 212: Gate Dream
    1.92f, // 213: Italo Arp
    -0.94f, // 214: Night Drive
    2.50f, // 215: Dembow Pluck
    -0.56f, // 216: Hyper Saw
    0.12f, // 217: Dark Rage
    0.17f, // 218: Soul Chords
    -1.12f, // 219: Analog Strings
    -0.12f, // 220: Digital Ice
    -1.00f, // 221: Glass Sync
    -1.10f, // 222: Warm Poly
    0.41f, // 223: Hard Sync
    0.37f, // 224: Trance Gate
    -1.62f, // 225: Vapor Wash
    0.24f, // 226: Future Chord
    0.13f, // 227: Drill Dark Pad
    -0.55f, // 228: Wavetable Lead
    -0.40f, // 229: Chord Synth
    2.50f, // 230: Arp Synth
    -0.86f, // 231: Dark Synth
    -0.25f, // 232: Syn Grand
    0.14f, // 233: Syn Bright
    -1.46f, // 234: Syn Soft Key
    1.42f, // 235: House Keys
    0.55f, // 236: Syn Upright
    -1.27f, // 237: Syn Felt
    1.20f, // 238: Syn Honky
    0.30f, // 239: Noir Keys
    0.98f, // 240: Syn Pop Key
    0.04f, // 241: Ballad Keys
    1.12f, // 242: Syn Tack
    -0.04f, // 243: Ghost Keys
    -1.37f, // 244: Royal Grand
    -1.02f, // 245: Concert Bright
    -0.24f, // 246: Felt Piano
    1.33f, // 247: Toy Piano
    -1.08f, // 248: Emotional Piano
    -0.67f, // 249: Piano Chord
    0.38f, // 250: Syn Nylon
    0.81f, // 251: Syn Steel
    -0.12f, // 252: Syn Clean Gtr
    1.59f, // 253: Syn Mute Gtr
    0.58f, // 254: Funk Gtr Syn
    -0.01f, // 255: Syn 12-String
    -0.36f, // 256: Syn Jazz Gtr
    -0.08f, // 257: Syn Spanish
    -0.29f, // 258: Chorus Gtr
    0.53f, // 259: Crunch Syn
    0.07f, // 260: Drive Lead Gtr
    -0.37f, // 261: Syn Slide
    0.24f, // 262: Chime Syn
    0.17f, // 263: Syn Bass Gtr
    1.23f, // 264: Pop Mute Gtr
    0.76f, // 265: Tropic Gtr
    -0.27f, // 266: Syn Acoustic
    1.51f, // 267: Muted Chug
    -0.18f, // 268: Nylon Soft
    -0.27f, // 269: Disco Guitar
    0.01f, // 270: Guitar Loop
    -0.03f, // 271: Glass Nylon
    -0.37f, // 272: Air Steel
    1.28f, // 273: Pearl Muted Gtr
    -0.39f, // 274: Clean Chorus Gtr
    -0.52f, // 275: Dream Strum
    0.26f, // 276: Indie Jangle
    -0.44f, // 277: Tape Guitar
    -0.37f, // 278: Lo-Fi Guitar
    -0.38f, // 279: Dream Lead Gtr
    -0.35f, // 280: Soft Slide Gtr
    1.64f, // 281: Palm Mute Pop
    -0.07f, // 282: Wide 12-String
    -0.56f, // 283: Neo Soul Gtr
    -0.44f, // 284: Bright Pick Gtr
    -0.18f, // 285: Ambient Guitar
    1.84f, // 286: Dream Pad
    1.60f, // 287: Warm Strings
    0.67f, // 288: Dark Pad
    -1.39f, // 289: Glass Pad
    -1.75f, // 290: Choir Air
    2.00f, // 291: Analog Sweep
    -2.12f, // 292: Ocean Pad
    1.23f, // 293: Cinema Strings
    -2.71f, // 294: Vapor Pad
    1.02f, // 295: Shimmer Pad
    1.51f, // 296: Tape Strings
    -0.66f, // 297: Cathedral
    -1.07f, // 298: Velvet Pad
    -2.26f, // 299: Aurora Pad
    -1.77f, // 300: Solar Winds
    1.77f, // 301: Ensemble Str
    1.87f, // 302: Soft Brass
    -0.49f, // 303: Boys Choir
    -0.14f, // 304: Grain Cloud
    -0.05f, // 305: Frost Pad
    0.90f, // 306: Juno Warmth
    -0.75f, // 307: Hollow Glass
    -0.19f, // 308: Nebula Drone
    0.21f, // 309: E-Bow Swell
    1.74f, // 310: Seraph Pad
    1.98f, // 311: Tension Bed
    -1.69f, // 312: Submerged
    -0.52f, // 313: Sunset Haze
    -2.81f, // 314: Retro Cosmos
    -0.70f, // 315: Bass Pad
    -2.91f, // 316: Sub Drone
    -0.18f, // 317: Soul Pad
    1.78f, // 318: Nebula Pad
    -0.78f, // 319: Ice Pad
    1.36f, // 320: Analog Wash
    -1.12f, // 321: Deep Space
    0.11f, // 322: Dusk Pad
    2.09f, // 323: Polar Lights
    1.99f, // 324: Cinema Swell
    1.50f, // 325: Big Pad
    1.74f, // 326: Spatial Pad
    0.05f, // 327: Liquid Air
    -0.94f, // 328: Glass Cloud
    2.50f, // 329: Sidechain Pad
    0.90f, // 330: Crystal Pluck
    0.34f, // 331: Syn Kalimba
    0.68f, // 332: Syn Marimba
    1.58f, // 333: Trance Pluck
    -1.19f, // 334: Syn Harp
    -1.26f, // 335: Syn Koto
    -0.31f, // 336: Syn Pizz
    0.25f, // 337: Syn Music Box
    -0.17f, // 338: Neon Pluck
    -1.23f, // 339: Syn Dulcimer
    -0.92f, // 340: Syn Steel Pan
    -0.86f, // 341: Syn Banjo
    -0.95f, // 342: Syn Sitar
    -0.48f, // 343: Tropic Pluck
    1.59f, // 344: Mallet Choir
    0.51f, // 345: Water Drop
    0.01f, // 346: Rubber Toy
    -0.54f, // 347: Syn Gamelan
    -0.01f, // 348: Felt Mallet
    -1.38f, // 349: Organ Pluck
    0.04f, // 350: Soul Pluck
    1.84f, // 351: Dance Pluck
    0.86f, // 352: Water Pluck
    1.10f, // 353: Bubble Pluck
    1.06f, // 354: Glass Pluck
    -0.68f, // 355: Amapiano Log
    0.45f, // 356: Droplet Pluck
    0.52f, // 357: Jersey Bounce
    -0.35f, // 358: Afro Kalimba
    -0.12f, // 359: Latin Guitar Pl
    0.23f, // 360: Aero Pluck
    -0.14f, // 361: Pearl Pluck
    0.00f, // 362: EP Keys
    -1.21f, // 363: Liquid Glass Keys
    0.61f, // 364: Pearl EP
    0.69f, // 365: Soft Keys
    -4.33f, // 366: House Organ
    -4.35f, // 367: Retro Organ
    2.50f, // 368: Funk Clav
    1.62f, // 369: Tine EP
    0.60f, // 370: Dusty Keys
    -0.40f, // 371: Wurli EP
    -2.41f, // 372: Pipe Organ
    -0.18f, // 373: DX Piano
    2.50f, // 374: Wah Clav
    -0.71f, // 375: Soft Celeste
    1.10f, // 376: Toy Keys
    -2.66f, // 377: Syn Accordion
    1.29f, // 378: Syn Harmonium
    -4.13f, // 379: Full Organ
    -2.51f, // 380: Perc Organ
    1.47f, // 381: Glass CP80
    -0.87f, // 382: Suitcase EP
    0.64f, // 383: Syn Harpsi
    0.83f, // 384: Soul Keys
    0.20f, // 385: K-RnB Keys
    0.36f, // 386: Dream Keys
    -1.02f, // 387: Soft EP
    0.25f, // 388: Night Keys
    2.50f, // 389: Chord Keys
    -0.93f, // 390: Dream Organ
    -1.10f, // 391: Amapiano Keys
    -0.74f, // 392: RnB Rhodes
    -0.83f, // 393: Gospel Organ
    0.48f, // 394: Soul Sample
    0.41f, // 395: Glass Bell
    -0.59f, // 396: Deep Bell
    0.07f, // 397: Syn Celesta
    -0.62f, // 398: Syn Tubular
    -0.79f, // 399: Syn Gong
    0.00f, // 400: Syn Handbell
    0.35f, // 401: Fairy Bell
    -0.79f, // 402: Syn Church
    -0.47f, // 403: Syn Vibes
    0.81f, // 404: Syn Glock
    0.77f, // 405: Crystal Bell
    2.23f, // 406: Toy Bell
    0.73f, // 407: Night Bell
    0.75f, // 408: Ice Bell
    -0.29f, // 409: Gamelan Glow
    -0.37f, // 410: Halo Bell
    -0.17f, // 411: Soft Glass Bell
    -0.39f, // 412: Syn Flute
    0.00f, // 413: Synth Brass
    2.50f, // 414: Brass Stab
    1.95f, // 415: Rave Hit
    -0.90f, // 416: Noise Riser
    -0.28f, // 417: Cinema Braam
    0.59f, // 418: Sub Drop
    0.73f, // 419: Vinyl Keys
    -0.81f, // 420: Tonal Wind
    0.04f, // 421: Sci-Fi Sweep
    -0.30f, // 422: Air Horn
    -0.35f, // 423: Horror Drone
    1.02f, // 424: Impact Hit
    -0.76f, // 425: Noise Sweep
    -0.19f, // 426: Reverse Cymbal
    0.15f, // 427: Ambient FX
    0.08f, // 428: Vinyl Crackle
    -0.42f, // 429: EDM Anthem Lead
    -0.18f, // 430: Neon Rave Lead
    -0.55f, // 431: Future Rave Stab
    -0.24f, // 432: Festival Arp
    -0.62f, // 433: Sidechain Chord
    -1.05f, // 434: Drop Atmos
    -0.86f, // 435: Rave Reese
    -0.38f, // 436: Sub Pulse
    0.00f, // 437: Neon Pulse Arp
    0.00f, // 438: Crystal Sequence
    0.00f, // 439: Trance Runner Arp
    0.00f, // 440: Future Pluck Arp
    0.00f, // 441: Acid Stepper Arp
    0.00f, // 442: Midnight Cascade
    0.00f, // 443: Glass Motion Arp
    0.00f, // 444: Festival Sequence
    0.00f, // 445: Retro Grid Arp
    0.00f, // 446: Tropic Motion Arp
    0.00f, // 447: Techno Pulse Arp
    0.00f, // 448: Airy Octave Arp
    -2.20f, // 449: Velvet EP
    -1.80f, // 450: Midnight Keys
    -2.60f, // 451: Glass EP
    -2.80f, // 452: Crystal Drop
    -1.80f, // 453: Silk Pluck
    -0.80f, // 454: Muted Pluck
    -3.20f, // 455: Pearl Bell
    -3.00f, // 456: Dream Music Box
    -1.20f, // 457: Wooden Mallet
    -3.50f, // 458: Soft Chime
    -4.00f, // 459: Cloud Pad
    -4.20f, // 460: Human Air
    -4.50f, // 461: Velvet Choir
    -4.20f, // 462: Frozen Voice
    -1.00f, // 463: Pure Sub
    -1.80f, // 464: Velvet Bass
    -2.00f, // 465: Slide 808
    -2.20f, // 466: Breath Lead
    -2.40f, // 467: Liquid Lead
    -2.00f, // 468: Warm Analog
}};
}
