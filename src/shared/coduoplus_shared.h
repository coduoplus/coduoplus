/*
 * coduoplus_shared.h - the values the Windows mod (mss32.dll) and the Linux server
 * library (coduoplus_sv.so, server/linux/) MUST agree on. One copy, included by both,
 * so a client and a server built from the same tree cannot drift apart.
 *
 * Plain C (no namespaces, no C++ types) on purpose: either side may include it.
 */
#ifndef CODUOPLUS_SHARED_H
#define CODUOPLUS_SHARED_H

/* Release, as shown in shortversion and the version gate's reject message. The
 * Windows build's single source of truth stays COD1RELOADED_VERSION (updater.h);
 * tests/site_check.cpp fails if the two disagree. */
#define CODUOPLUS_VERSION        "0.2.0"

/* userinfo "coduoplus": the feature level ("1.6" = 16). The server's version gate
 * refuses CoDUO+ clients below it; vanilla clients carry no such key and are let in. */
#define CODUOPLUS_NET_VERSION    16
#define CODUOPLUS_USERINFO_KEY   "coduoplus"

/* ---- swing fix (gameplay/swing_fix.*, server/linux/coduoplus_sv.cpp) ------------
 * BG_PlayerAngles constants. Vanilla in brackets. The client applies them to its
 * cgame only while the server publishes SWING_SERVERINFO_CVAR "1", and a server
 * publishes it only after applying the very same values to its own game module, so
 * the pose players see is the pose the server's bullet traces test. */
#define SWING_LEGS_TOLERANCE      0.0f   /* [40.0]  legs swingTolerance               */
#define SWING_TORSO_PITCH_SPEED   1.0f   /* [0.15]  the lean swing channel's speed      */
#define SWING_TORSO_YAW_SPEED     1.0f   /* [bg_swingSpeed]  torso yaw only, legs keep it */
#define SWING_TORSO_YAW_MOVEFRAC  0.0f   /* [0.3]   movementYaw pull on the torso target */
#define SWING_SERVERINFO_CVAR     "sv_coduoplus_swing"

/* ---- competitive spec (netcode/competitive*.cpp, server/linux) ------------------ */
#define COMP_SPEC_CVAR            "sv_competitive"   /* then sv_competitive2..4 */
#define COMP_SV_CHUNK_MAX_C       220                /* per-cvar payload (< 256)  */
#define COMP_SV_CHUNKS_C          4

/* ---- lag compensation (shared/antilag_core.*, netcode/antilag.cpp, server/linux) -- */
#define ANTILAG_CVAR            "g_antilag"       /* 0 off, 1 on, 2 on + log every rewind */
#define ANTILAG_MAXMS_CVAR      "g_antilagMaxMs"  /* furthest a shot is rewound          */
#define ANTILAG_POSE_CVAR       "g_antilagPose"   /* 1 rewind the pose too, 0 placement   */
#define ANTILAG_POSE_DEFAULT    1
#define ANTILAG_DEFAULT         1
#define ANTILAG_MAXMS_DEFAULT   300

/* engine cvar flags (Quake 3, identical in CoDUOMP.exe and coduo_lnxded) */
#define CODUOPLUS_CVAR_SERVERINFO 0x04
#define CODUOPLUS_CVAR_SYSTEMINFO 0x08
#define CODUOPLUS_CVAR_ROM        0x40

#endif /* CODUOPLUS_SHARED_H */
