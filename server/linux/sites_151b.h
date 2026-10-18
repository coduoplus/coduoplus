/*
 * sites_151b.h - every address coduoplus_sv.so touches in the CoD:UO 1.51b Linux
 * dedicated server (icculus build, 2005-03-22), with the exact bytes expected there.
 *
 *   coduo_lnxded            sha256 24b0d826...61f1  ELF32 exec, base 0x08048000, no PIE
 *   uo/game.mp.uo.i386.so   sha256 4d2391c1...acb7  ELF32 PIC .so, GOT 0xb3818
 *
 * Used twice: by the library at run time (it patches nothing unless EVERY expected
 * byte sequence of a group is present - another build simply gets no CoDUO+ features)
 * and by tests/site_check_linux.cpp, which checks the same table against the files
 * offline. How each site was found: docs/LINUX.md.
 *
 * Plain C, no OS headers: the Windows-built test includes it too.
 */
#ifndef CODUOPLUS_SITES_151B_H
#define CODUOPLUS_SITES_151B_H

#include <stdint.h>

typedef struct {
    const char*    what;
    uint32_t       at;          /* exe: absolute VA; game module: RVA            */
    uint8_t        len;
    uint8_t        bytes[16];   /* what must be there before we write anything   */
} coduo_site_t;

/* ============================== coduo_lnxded ============================== */
#define LNX_EXE_BASE 0x08048000u

/* SV_UserinfoChanged: snaps = atoi(Info_ValueForKey(userinfo, "snaps")) clamped
 * to [1, 30], snapshotMsec (client_t+0x9b038) = 1000 / snaps - the same clamp and
 * field as CoDUOMP.exe 0x45bbf4. Both imm8/imm32 30 -> 40.
 *   808d48d  83 7d f8 1e            cmp  [ebp-8],0x1e
 *   808d491  7e 07                  jle  +7
 *   808d493  c7 45 f8 1e 00 00 00   mov  [ebp-8],0x1e
 *   808d49d  ba e8 03 00 00         mov  edx,1000         (then idiv [ebp-8])        */
#define LNX_SNAPS_CMP_IMM  0x0808d490u
#define LNX_SNAPS_MOV_IMM  0x0808d496u

/* Com_Init: Cvar_Get("shortversion", "1.51", 0x44 CVAR_ROM|CVAR_SERVERINFO):
 *   80719c9  c7 44 24 08 44 00 00 00  mov [esp+8],0x44
 *   80719d1  c7 44 24 04 9a ed 0d 08  mov [esp+4],0x80ded9a   ; "1.51"  <- operand
 *   80719d9  c7 04 24 5c ef 0d 08     mov [esp],0x80def5c     ; "shortversion"
 * Operand repointed at "CoDUO+ <version>", as the Windows mod does: server browsers
 * and getstatus show which servers run it.                                          */
#define LNX_SHORTVERSION_OPERAND 0x080719d5u
#define LNX_SHORTVERSION_ORIG    0x080ded9au   /* "1.51" */

/* Sys_LoadDll dlopen()s the game module (RTLD_NOW = 2, 80c9133). The engine calls
 * dlopen through its own PLT, so the library repoints that GOT slot and sees every
 * game-module load the moment it returns - before the engine calls dllEntry/vmMain.
 *   8049f1c  ff 25 cc e1 0f 08  jmp [0x80fe1cc]     (R_386_JUMP_SLOT dlopen@GLIBC_2.1) */
#define LNX_EXE_GOT_DLOPEN 0x080fe1ccu

/* Server DObj skeleton cache (lag compensation rewinds poses through it). Same
 * structure as CoDUOMP.exe (0x968918 / 0x969228 / 0x4927eb0), see
 * shared/antilag_core.h. The game's DObj syscalls look an entity up with
 *   80725cb  ... movsx eax,WORD PTR [eax+eax*1+0x8252d00] ; imul eax,eax,0x5c ;
 *            add eax,0x823b300                    (entnum -> DObj info, 0 = none)
 * and syscall 111 (CreateSkelForBones, 808ea25) checks the skeleton against
 *   808ea39  a1 04 87 48 08   mov eax,ds:0x8488704   (the frame counter)
 * allocating 0x30 + 96 * info[0x17] bytes when it is stale (80c6a29 movzx [eax+0x17]). */
#define LNX_DOBJ_INDEX                0x08252d00u   /* short[] entnum -> info index       */
#define LNX_DOBJ_INFO                 0x0823b300u   /* info[], 0x5c each: +4 skel +8 frame */
#define LNX_DOBJ_FRAME                0x08488704u   /* skeleton frame counter              */

/* The engine's only inode-sensitive file APIs, both the 2005 non-LFS (32-bit) ones:
 * glibc fails them with EOVERFLOW when an inode number needs more than 32 bits (drvfs /
 * 9p Windows drives under WSL or Docker Desktop, big XFS/btrfs disks), and the engine
 * then finds "0 files in pk3 files". Repointed to wrappers over the 64-bit calls that
 * fold the inode to 32 bits (nothing in the engine uses it). closedir: per-DIR buffers.
 *   8049a8c  ff 25 a8 e0 0f 08  jmp [0x80fe0a8]   __xstat@GLIBC_2.0
 *   8049e9c  ff 25 ac e1 0f 08  jmp [0x80fe1ac]   readdir@GLIBC_2.0
 *   8049cac  ff 25 30 e1 0f 08  jmp [0x80fe130]   closedir@GLIBC_2.0              */
#define LNX_EXE_GOT_XSTAT    0x080fe0a8u
#define LNX_EXE_GOT_READDIR  0x080fe1acu
#define LNX_EXE_GOT_CLOSEDIR 0x080fe130u

static const coduo_site_t LNX_EXE_SITES[] = {
    { "dlopen plt",   0x08049f1c, 6,  { 0xff, 0x25, 0xcc, 0xe1, 0x0f, 0x08 } },
    { "__xstat plt",  0x08049a8c, 6,  { 0xff, 0x25, 0xa8, 0xe0, 0x0f, 0x08 } },
    { "readdir plt",  0x08049e9c, 6,  { 0xff, 0x25, 0xac, 0xe1, 0x0f, 0x08 } },
    { "closedir plt", 0x08049cac, 6,  { 0xff, 0x25, 0x30, 0xe1, 0x0f, 0x08 } },
    { "snaps clamp",  0x0808d48d, 15, { 0x83, 0x7d, 0xf8, 0x1e, 0x7e, 0x07,
                                        0xc7, 0x45, 0xf8, 0x1e, 0x00, 0x00, 0x00, 0x8b, 0x4d } },
    { "snaps 1000/x", 0x0808d49d, 5,  { 0xba, 0xe8, 0x03, 0x00, 0x00 } },
    { "shortversion", 0x080719c9, 16, { 0xc7, 0x44, 0x24, 0x08, 0x44, 0x00, 0x00, 0x00,
                                        0xc7, 0x44, 0x24, 0x04, 0x9a, 0xed, 0x0d, 0x08 } },
    { "1.51 string",  0x080ded9a, 5,  { '1', '.', '5', '1', 0 } },
    { "dobj lookup",  0x080725d4, 16, { 0x66, 0x83, 0xbc, 0x00, 0x00, 0x2d, 0x25, 0x08,
                                        0x00, 0x74, 0x18, 0x8b, 0x45, 0x08, 0x0f, 0xbf } },
    { "dobj info",    0x080725ea, 8,  { 0x6b, 0xc0, 0x5c, 0x05, 0x00, 0xb3, 0x23, 0x08 } },
    { "skel frame",   0x0808ea39, 5,  { 0xa1, 0x04, 0x87, 0x48, 0x08 } },
    { "skel bones",   0x080c6a2d, 4,  { 0x0f, 0xb6, 0x40, 0x17 } },
};

/* ============================ game.mp.uo.i386.so ============================ */
/* Exported functions/data we use (nm -D), checked against dlsym at run time. */
#define LNX_G_VMMAIN                  0x00056ac0u
#define LNX_G_CLIENTCONNECT           0x00044ab4u
#define LNX_G_RUNFRAME                0x00059c21u
#define LNX_G_TRAP_CVAR_REGISTER      0x00073cd2u   /* syscall 4  (vmCvar*, name, def, flags) */
#define LNX_G_TRAP_CVAR_SET           0x00073d43u   /* syscall 6  (name, value)               */
#define LNX_G_TRAP_CVAR_STRINGBUF     0x00073de4u   /* trap_Cvar_VariableStringBuffer          */
#define LNX_G_TRAP_GETUSERINFO        0x00074004u   /* syscall 33 (clientNum, buf, size)       */
#define LNX_G_BG_SWINGSPEED           0x0042f660u   /* vmCvar_t bg_swingSpeed, .value at +8    */
#define LNX_G_BULLET_FIRE             0x0007a8afu   /* (attacker, spread, damage, wp, self)    */
#define LNX_G_TRAP_LINKENTITY         0x0007457fu   /* syscall: SV_LinkEntity(gentity*)        */
#define LNX_G_ENTITIES                0x0035b6a0u   /* gentity_t g_entities[1024], 0x34c each  */
#define LNX_G_DOBJCALCPOSE            0x00078f4fu   /* G_DObjCalcPose(gentity*): every bone    */
#define LNX_G_GOT                     0x000b3818u   /* DT_PLTGOT = what ebx holds in PIC code  */

/* vmMain calls both through the PLT (`jmp [ebx+slot]`); repointing the GOT slot is the
 * ELF equivalent of the Windows IAT hook - it catches every call, and the original is
 * called through the address dlsym gives. No .text is modified for these two.
 *   56b43 call ClientConnect@plt (186c4: ff a3 e0 07 00 00  jmp [ebx+0x7e0])
 *   56bca call G_RunFrame@plt    (18464: ff a3 48 07 00 00  jmp [ebx+0x748])        */
#define LNX_G_GOT_CLIENTCONNECT       0x000b3ff8u   /* R_386_JUMP_SLOT ClientConnect */
#define LNX_G_GOT_RUNFRAME            0x000b3f60u   /* R_386_JUMP_SLOT G_RunFrame    */

/* Lag compensation: every bullet a player (or a turret/vehicle gunner) fires goes
 * through Bullet_Fire@plt (FireWeapon 7be48, turret 5ab94, vehicles 8f44c 8fb8d 900c3);
 * only Bullet_Fire itself calls Bullet_Fire_Extended, whose penetration recursion stays
 * inside. So the Bullet_Fire GOT slot is the one root hook, same mechanism as above.
 *   16e34  ff a3 bc 01 00 00  jmp [ebx+0x1bc]                                     */
#define LNX_G_GOT_BULLET_FIRE         0x000b39d4u   /* R_386_JUMP_SLOT Bullet_Fire   */

/* BG_PlayerAngles (static, ~0x1dc00): the four swing constants, the SAME four the
 * Windows mod changes in uo_cgame / uo_game (src/gameplay/swing_fix.h) and upstream
 * mirrors on its CoD1 Linux server (cod1plushookserver swing_sync.c). GCC passes the
 * BG_SwingAngles(angle, swingTol, clampTol, speed, &angle, &swinging) args as stores:
 *
 * 1. movementYaw pull, 1de0b:  d9 45 e8           fld  [ebp-0x18]      (movementYaw)
 *                              d9 83 40 5e fe ff  fld  [ebx-0x1a1c0]   (.rodata 0.3f, ONE reader)
 *                              de c9 / d9 45 bc / de c1  fmulp ; fld view ; faddp
 *    -> `fld [abs]` (d9 05 imm32) on the library's own float (SWING_TORSO_YAW_MOVEFRAC).
 * 2. torso yaw speed, 1de9e:   8b 83 ec 0e 00 00  mov eax,[ebx+0xeec]  (GOT -> bg_swingSpeed)
 *                              8b 40 08           mov eax,[eax+8]      (.value)
 *                              89 44 24 0c        mov [esp+0xc],eax    (speed arg)
 *    first of the cvar's FIVE readers (torso; the four after are the legs, left alone)
 *    -> `mov eax, &our_float - 8 ; nop` (b8 imm32 90): [eax+8] reads our float.
 * 3. legs swingTolerance, 1e040: c7 44 24 04 00 00 20 42  mov [esp+4],40.0f -> 0.0
 * 4. lean channel speed, 1e16b:  c7 44 24 0c 9a 99 19 3e  mov [esp+0xc],0.15f -> 1.0
 *                                (followed by mov [esp+8],45.0f, the clamp)           */
#define LNX_G_SW_MOVEFRAC_FLD         0x0001de0eu
#define LNX_G_SW_YAW_LOAD             0x0001de9eu
#define LNX_G_SW_LEGS_TOL_IMM         0x0001e044u
#define LNX_G_SW_PITCH_SPEED_IMM      0x0001e16fu

static const coduo_site_t LNX_GAME_SITES[] = {
    { "movefrac fld",  0x0001de0b, 14, { 0xd9, 0x45, 0xe8, 0xd9, 0x83, 0x40, 0x5e, 0xfe, 0xff,
                                         0xde, 0xc9, 0xd9, 0x45, 0xbc } },
    { "yaw speed",     0x0001de9e, 13, { 0x8b, 0x83, 0xec, 0x0e, 0x00, 0x00, 0x8b, 0x40, 0x08,
                                         0x89, 0x44, 0x24, 0x0c } },
    { "legs tol 40",   0x0001e040, 8,  { 0xc7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x20, 0x42 } },
    { "pitch 0.15",    0x0001e16b, 16, { 0xc7, 0x44, 0x24, 0x0c, 0x9a, 0x99, 0x19, 0x3e,
                                         0xc7, 0x44, 0x24, 0x08, 0x00, 0x00, 0x34, 0x42 } },
    { "plt ClientCon", 0x000186c4, 6,  { 0xff, 0xa3, 0xe0, 0x07, 0x00, 0x00 } },
    { "plt G_RunFrame",0x00018464, 6,  { 0xff, 0xa3, 0x48, 0x07, 0x00, 0x00 } },
    { "vmMain call CC",0x00056b43, 5,  { 0xe8, 0x7c, 0x1b, 0xfc, 0xff } },
    { "vmMain call RF",0x00056bca, 5,  { 0xe8, 0x95, 0x18, 0xfc, 0xff } },
    { "plt Bullet_Fire",0x00016e34, 6, { 0xff, 0xa3, 0xbc, 0x01, 0x00, 0x00 } },
    { "FireWeapon->BF",0x0007be48, 5,  { 0xe8, 0xe7, 0xaf, 0xf9, 0xff } },
};

#endif /* CODUOPLUS_SITES_151B_H */
