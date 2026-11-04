/* Host test for patch 7032 (the offline network model: a console with no link, not signed in to PSN). Linked against
 * the patched network sources, each built as its own object (sceNp.h and sceNpBasic.h cannot share a translation unit),
 * so the prototypes below are written out here with the sources' own types; every pointer the guest passes is a guest
 * address. The error values are spelled as numbers on purpose: they are the facts under test (the RE notes).
 * Run with port/rc/tests/run_net_test.sh (gcc and clang). */
#include "rc_vm_stubs.h"
#include "runtime/ppu/ppu_context.h"
#include "ps3emu/nid.h"
#include <stdio.h>
#include <string.h>

typedef int32_t s32t;
#define EQ(call, want) do { s32t got_ = (s32t)(call); if (got_ != (s32t)(want)) { \
    fprintf(stderr, "%s:%d: %s = 0x%08x, want 0x%08x\n", __FILE__, __LINE__, #call, (uint32_t)got_, (uint32_t)(want)); \
    return 1; } } while (0)
#define OK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

/* ---- guest memory: a 1 MB window; a bump allocator for net_offline's static buffer ---- */
static uint8_t s_mem[1u << 20];
static uint32_t s_brk = 0x80000;
static unsigned int guest_alloc(unsigned int size, unsigned int align)
{
    s_brk = (s_brk + align - 1) & ~(align - 1);
    unsigned int ea = s_brk;
    s_brk += size;
    return ea;
}
static uint32_t gstr(uint32_t ea, const char* s) { memcpy(s_mem + ea, s, strlen(s) + 1); return ea; }
static uint32_t rd32(uint32_t ea) { return vm_read32(ea); }

/* ---- runtime hooks the sources call ---- */
#define MAX_CTX_FN 64
static struct { uint32_t nid; void (*fn)(ppu_context*); } s_ctx_fn[MAX_CTX_FN];
static int s_nctx;
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*))
{
    (void)name;
    for (int i = 0; i < s_nctx; i++) if (s_ctx_fn[i].nid == nid) return;   /* first registration wins */
    s_ctx_fn[s_nctx].nid = nid; s_ctx_fn[s_nctx].fn = fn; s_nctx++;
}
static int s_errno;
static void set_errno(int e) { s_errno = e; }
/* Call a registered context handler by name, with r3.. and the stack args from 9 on. */
static uint64_t call_ctx(const char* name, int n, const uint64_t* a)
{
    uint32_t nid = ps3_compute_nid(name);
    ppu_context ctx; memset(&ctx, 0, sizeof ctx);
    ctx.gpr[1] = 0xF0000;   /* stack: args 9.. at sp + 112 + 8*(n-9) */
    for (int i = 0; i < n; i++) {
        if (i < 8) ctx.gpr[3 + i] = a[i];
        else vm_write64((uint32_t)ctx.gpr[1] + 112u + 8u * (uint32_t)(i - 8), a[i]);
    }
    for (int i = 0; i < s_nctx; i++) if (s_ctx_fn[i].nid == nid) { s_ctx_fn[i].fn(&ctx); return ctx.gpr[3]; }
    fprintf(stderr, "no handler for %s\n", name);
    return 0xDEAD;
}
static int registered(const char* name)
{
    uint32_t nid = ps3_compute_nid(name);
    for (int i = 0; i < s_nctx; i++) if (s_ctx_fn[i].nid == nid) return 1;
    return 0;
}

static uint64_t s_cb_args[8]; static uint32_t s_cb_opd; static int s_cb_count;
int32_t cellSysutilQueueGuestCallbackArgs(uint32_t opd, const uint64_t args[8])
{ s_cb_opd = opd; memcpy(s_cb_args, args, sizeof s_cb_args); s_cb_count++; return 0; }
void cellSysutilQueueEvent(int slot, uint32_t status, uint32_t param) { (void)slot; (void)status; (void)param; }
void edat_add_klicensee(const uint8_t klic[16]) { (void)klic; }

/* psnr: no server; s_psnr toggles "signed in to psnr" for the online-side checks */
static int s_psnr;
int np_psnr_enabled(void) { return s_psnr; }
int np_psnr_connect(const char* comm_id) { (void)comm_id; return -1; }
int np_psnr_connected(void) { return 0; }
const char* np_psnr_identity(void) { return NULL; }
const char* np_psnr_online_id(void) { return "rc-test"; }
uint32_t np_psnr_request(uint8_t type, const void* body, uint32_t len, void* fn, void* user)
{ (void)type; (void)body; (void)len; (void)fn; (void)user; return 0; }
void np_psnr_on_push(void (*fn)(const void*)) { (void)fn; }
void np_psnr_on_tick(void (*fn)(void)) { (void)fn; }
void np_psnr_pump(void) {}
void np_psnr_punch(uint32_t user, const uint8_t ip[4], uint16_t port) { (void)user; (void)ip; (void)port; }

/* ---- the functions under test (types as in the sources) ---- */
void ps3_net_offline_register(unsigned int (*)(unsigned int, unsigned int), void (*)(int));
void ps3_net_set_online(int on);
void np_score_register_ctx(void);
void np_tus_register_ctx(void);

s32t cellHttpInit(void* pool, uint32_t poolSize);
s32t cellHttpCreateClient(uint32_t* clientId);
s32t cellHttpClientSetUserAgent(uint32_t, uint32_t);
s32t cellHttpClientSetHeader(uint32_t, uint32_t);
s32t cellHttpClientSetProxy(uint32_t, uint32_t);
s32t cellHttpClientSetVersion(uint32_t, uint32_t, uint32_t);
s32t cellHttpClientSetAutoRedirect(uint32_t, uint32_t);
s32t cellHttpClientSetAutoAuthentication(uint32_t, uint32_t);
s32t cellHttpClientSetPipeline(uint32_t, uint32_t);
s32t cellHttpClientSetKeepAlive(uint32_t, uint32_t);
s32t cellHttpClientSetCookieStatus(uint32_t, uint32_t);
s32t cellHttpClientSetResponseBufferMax(uint32_t, uint32_t);
s32t cellHttpClientSetConnTimeout(uint32_t, int64_t);
s32t cellHttpClientSetSendTimeout(uint32_t, int64_t);
s32t cellHttpClientSetRecvTimeout(uint32_t, int64_t);
s32t cellHttpClientSetSslCallback(uint32_t, uint32_t, uint32_t);
s32t cellHttpClientCloseAllConnections(uint32_t);
s32t cellHttpClientPollConnections(uint32_t, uint32_t, int64_t);
s32t cellHttpRequestSetContentLength(uint32_t, uint64_t);

s32t cellGameUpdateInit(void);
s32t cellGameUpdateTerm(void);
s32t cellGameUpdateCheckStartAsync(uint32_t, uint32_t, uint32_t);
s32t cellGameUpdateCheckStartWithoutDialogAsync(uint32_t, uint32_t);
s32t cellGameUpdateCheckFinishAsync(uint32_t, uint32_t);

s32t cellSslInit(void* pool, uint32_t poolSize);
s32t cellSslCertGetSubjectName(uint32_t, uint32_t);
s32t cellSslCertGetIssuerName(uint32_t, uint32_t);
s32t cellSslCertGetNameEntryCount(uint32_t, uint32_t);
s32t cellSslCertGetNameEntryInfo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, s32t);
s32t cellSslCertGetRsaPublicKeyModulus(uint32_t, uint32_t, uint32_t);
s32t cellSslCertGetRsaPublicKeyExponent(uint32_t, uint32_t, uint32_t);

s32t cellRudpInit(uint32_t);
s32t cellRudpEnd(void);
s32t cellRudpEnableInternalIOThread(uint32_t, uint32_t);
s32t cellRudpSetEventHandler(uint32_t, uint32_t);
s32t cellRudpCreateContext(uint32_t, uint32_t, uint32_t);
s32t cellRudpSetOption(s32t, s32t, uint32_t, uint32_t);
s32t cellRudpGetContextStatus(s32t, uint32_t, uint32_t);
s32t cellRudpGetRemoteInfo(s32t, uint32_t, uint32_t, uint32_t);
s32t cellRudpBind(s32t, s32t, uint16_t, uint8_t);
s32t cellRudpInitiate(s32t, uint32_t, uint32_t, uint16_t);
s32t cellRudpTerminate(s32t);
s32t cellRudpRead(s32t, uint32_t, uint32_t, uint8_t, uint32_t);
s32t cellRudpWrite(s32t, uint32_t, uint32_t, uint8_t);
s32t cellRudpGetSizeReadable(s32t);
s32t cellRudpGetSizeWritable(s32t);

s32t sceNpInit(uint32_t poolSize, void* poolPtr);
s32t sceNpManagerGetAvatarUrl(uint32_t);
s32t sceNpManagerSubSignin(uint32_t, uint32_t, uint32_t, s32t);
s32t sceNpManagerSubSignout(uint32_t);
s32t sceNpScoreInit(void);
s32t sceNpScoreCreateTitleCtx(uint32_t, uint32_t, uint32_t);
s32t sceNpScoreCreateTransactionCtx(s32t);
s32t sceNpScorePollAsync(s32t, uint32_t);

s32t sceNpBasicGetEvent(uint32_t* event, void* from, void* data, uint32_t* size);
s32t sceNpBasicRegisterHandler(void* handler, void* presenceHandler, void* arg);
s32t sceNpBasicUnregisterHandler(void);
s32t sceNpBasicRegisterContextSensitiveHandler(const void* context, void* handler, void* arg);
s32t sceNpBasicGetPlayersHistoryEntryCount(uint32_t options, uint32_t* count);
s32t sceNpBasicSendMessageGui(uint32_t, uint32_t);
s32t sceNpBasicAddFriend(uint32_t, uint32_t, uint32_t);
s32t sceNpBasicAddPlayersHistory(uint32_t, uint32_t);
s32t sceNpBasicGetFriendPresenceByIndex(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpBasicGetFriendPresenceByNpId2(uint32_t, uint32_t, uint32_t);
s32t sceNpBasicGetBlockListEntry(uint32_t, uint32_t);
s32t sceNpBasicRecvMessageAttachmentLoad(uint32_t, uint32_t, uint32_t);
s32t sceNpBasicRecvMessageCustom(uint32_t, uint32_t, uint32_t);

s32t sceNpSignalingInit(void);
s32t sceNpSignalingCreateCtx(const void* npId, void* handler, void* arg, uint32_t* ctxId);
s32t sceNpSignalingDeactivateConnection(uint32_t, uint32_t);
s32t sceNpSignalingTerminateConnection(uint32_t, uint32_t);

s32t sceNpLookupInit(void);
s32t sceNpLookupCreateTitleCtx(uint32_t, uint32_t);
s32t sceNpLookupCreateTransactionCtx(s32t);
s32t sceNpLookupNpIdAsync(s32t, uint32_t, uint32_t, s32t, uint32_t);
s32t sceNpLookupTitleSmallStorageAsync(s32t, uint32_t, uint32_t, uint32_t, s32t, uint32_t);

s32t sceNpMatching2Init(uint32_t, s32t, s32t);
s32t sceNpMatching2CreateContext(uint32_t, uint32_t, uint32_t, uint32_t, s32t);
s32t sceNpMatching2ClearEventData(uint16_t, uint32_t);
s32t sceNpMatching2GetUserInfoList(uint16_t, uint32_t, uint32_t, uint32_t);
s32t sceNpMatching2GetRoomDataExternalList(uint16_t, uint32_t, uint32_t, uint32_t);
s32t sceNpMatching2SetRoomMemberDataInternal(uint16_t, uint32_t, uint32_t, uint32_t);

s32t sceNpCommerce2Init(void);
s32t sceNpCommerce2CreateCtx(uint32_t, const void*, uint32_t, uint32_t, void* ctx);
s32t sceNpCommerce2DoProductBrowseStartAsync(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpCommerce2DoProductBrowseFinishAsync(uint32_t);

s32t sceNpTusInit(s32t);
s32t sceNpTusTerm(void);
s32t sceNpTusCreateTitleCtx(uint32_t, uint32_t, uint32_t);
s32t sceNpTusDestroyTitleCtx(s32t);
s32t sceNpTusCreateTransactionCtx(s32t);
s32t sceNpTusDestroyTransactionCtx(s32t);
s32t sceNpTusAbortTransaction(s32t);
s32t sceNpTusPollAsync(s32t, uint32_t);
s32t sceNpTusSetMultiSlotVariableAsync(s32t, uint32_t, uint32_t, uint32_t, s32t, uint32_t);
s32t sceNpTusGetMultiSlotVariableAsync(s32t, uint32_t, uint32_t, uint32_t, uint32_t, s32t, uint32_t);
s32t sceNpTusGetMultiUserVariableAsync(s32t, uint32_t, s32t, uint32_t, uint32_t, s32t, uint32_t);
s32t sceNpTusGetDataAsync(s32t, uint32_t, s32t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

s32t sceNpClansInit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansTerm(void);
s32t sceNpClansCreateRequest(uint32_t, uint64_t);
s32t sceNpClansDestroyRequest(uint32_t);
s32t sceNpClansCreateClan(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansGetClanList(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansGetClanInfo(uint32_t, uint32_t, uint32_t);
s32t sceNpClansGetMemberList(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansSearchByName(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansLeaveClan(uint32_t, uint32_t);
s32t sceNpClansDisbandClan(uint32_t, uint32_t);
s32t sceNpClansChangeMemberRole(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansSendInvitation(uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansRetrieveAnnouncements(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansPostAnnouncement(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansPostChallenge(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
s32t sceNpClansRemovePostedChallenge(uint32_t, uint32_t, uint32_t, uint32_t);

/* ------------------------------------------------------------------------------------------------------------- */

static int test_sys_net(void)
{
    ps3_net_offline_register(guest_alloc, set_errno);
    const char* names[] = { "socket", "bind", "listen", "connect", "accept", "send", "shutdown", "socketclose",
                            "getsockname", "setsockopt", "getsockopt", "inet_ntoa", "inet_ntop", "inet_addr",
                            "inet_aton", "sys_net_open_dump", "sys_net_read_dump", "sys_net_close_dump" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) OK(registered(names[i]));

    uint64_t a[8];
    a[0] = 2; a[1] = 2; a[2] = 0;                                   /* AF_INET, SOCK_DGRAM */
    s32t s = (s32t)call_ctx("socket", 3, a);
    OK(s >= 3);
    a[0] = 2; a[1] = 1; a[2] = 0;
    s32t t = (s32t)call_ctx("socket", 3, a);
    OK(t == s + 1);

    /* bind keeps the address; getsockname gives it back as a 16-byte sockaddr_in */
    uint32_t sa = 0x1000; memset(s_mem + sa, 0, 16);
    vm_write8(sa, 16); vm_write8(sa + 1, 2); vm_write16(sa + 2, 3658); vm_write32(sa + 4, 0);
    a[0] = (uint64_t)s; a[1] = sa; a[2] = 16;
    EQ(call_ctx("bind", 3, a), 0);
    uint32_t out = 0x1100, lenp = 0x1200; vm_write32(lenp, 16);
    a[0] = (uint64_t)s; a[1] = out; a[2] = lenp;
    EQ(call_ctx("getsockname", 3, a), 0);
    OK(vm_read8(out) == 16 && vm_read8(out + 1) == 2 && vm_read16(out + 2) == 3658 && rd32(lenp) == 16);

    /* options: SO_TYPE is the type; a set option reads back; one never set reads 0 */
    uint32_t val = 0x1300; vm_write32(lenp, 4);
    a[0] = (uint64_t)t; a[1] = 0xffff; a[2] = 0x1008; a[3] = val; a[4] = lenp;
    EQ(call_ctx("getsockopt", 5, a), 0); OK(rd32(val) == 1);
    vm_write32(val, 65536);
    a[0] = (uint64_t)s; a[1] = 0xffff; a[2] = 0x1001; a[3] = val; a[4] = 4;   /* SO_SNDBUF */
    EQ(call_ctx("setsockopt", 5, a), 0);
    vm_write32(val, 0); vm_write32(lenp, 4);
    a[4] = lenp; EQ(call_ctx("getsockopt", 5, a), 0); OK(rd32(val) == 65536);
    a[2] = 0x1002; vm_write32(val, 7); EQ(call_ctx("getsockopt", 5, a), 0); OK(rd32(val) == 0);

    /* nothing reaches a peer: -1 and the BSD errno */
    a[0] = (uint64_t)t; a[1] = sa; a[2] = 16;
    s_errno = 0; EQ(call_ctx("connect", 3, a), -1); OK(s_errno == 51);              /* ENETUNREACH */
    a[0] = (uint64_t)t; a[1] = 0; a[2] = 0;
    EQ(call_ctx("listen", 2, a), 0);
    s_errno = 0; EQ(call_ctx("accept", 3, a), -1); OK(s_errno == 35);               /* EWOULDBLOCK */
    a[0] = (uint64_t)t; a[1] = 0x1400; a[2] = 4; a[3] = 0;
    s_errno = 0; EQ(call_ctx("send", 4, a), -1); OK(s_errno == 57);                 /* ENOTCONN */
    a[1] = 2; s_errno = 0; EQ(call_ctx("shutdown", 2, a), -1); OK(s_errno == 57);
    /* a closed socket is a bad descriptor */
    a[0] = (uint64_t)t; EQ(call_ctx("socketclose", 1, a), 0);
    s_errno = 0; EQ(call_ctx("socketclose", 1, a), -1); OK(s_errno == 9);           /* EBADF */
    a[0] = 999; s_errno = 0; EQ(call_ctx("listen", 2, a), -1); OK(s_errno == 9);

    /* address conversion */
    a[0] = gstr(0x2000, "192.168.1.20");
    OK((uint32_t)call_ctx("inet_addr", 1, a) == 0xC0A80114u);
    a[0] = gstr(0x2000, "10.1"); OK((uint32_t)call_ctx("inet_addr", 1, a) == 0x0A000001u);
    a[0] = gstr(0x2000, "0x7f.1"); OK((uint32_t)call_ctx("inet_addr", 1, a) == 0x7F000001u);
    a[0] = gstr(0x2000, "1.2.3.256"); OK((uint32_t)call_ctx("inet_addr", 1, a) == 0xFFFFFFFFu);
    a[0] = gstr(0x2000, "nope"); a[1] = 0x2100; vm_write32(0x2100, 0x12345678);
    EQ(call_ctx("inet_aton", 2, a), 0); OK(rd32(0x2100) == 0x12345678);
    a[0] = gstr(0x2000, "127.0.0.1"); EQ(call_ctx("inet_aton", 2, a), 1); OK(rd32(0x2100) == 0x7F000001u);
    a[0] = 0xC0A80001u;
    uint32_t p = (uint32_t)call_ctx("inet_ntoa", 1, a);
    OK(p && strcmp((const char*)s_mem + p, "192.168.0.1") == 0);
    vm_write32(0x2200, 0x0A000002u);
    a[0] = 2; a[1] = 0x2200; a[2] = 0x2300; a[3] = 16;
    OK((uint32_t)call_ctx("inet_ntop", 4, a) == 0x2300 && strcmp((const char*)s_mem + 0x2300, "10.0.0.2") == 0);
    a[3] = 4; s_errno = 0; OK(call_ctx("inet_ntop", 4, a) == 0 && s_errno == 28);    /* ENOSPC */
    a[0] = 28; a[3] = 64; s_errno = 0; OK(call_ctx("inet_ntop", 4, a) == 0 && s_errno == 47);   /* EAFNOSUPPORT */

    /* the packet dump opens and stays empty */
    a[0] = 0x1000; a[1] = 0; EQ(call_ctx("sys_net_open_dump", 2, a), 0);
    a[0] = 0; a[1] = 0x3000; a[2] = 64; a[3] = 0x3100; vm_write32(0x3100, 9);
    EQ(call_ctx("sys_net_read_dump", 4, a), 0); OK(rd32(0x3100) == 0);
    a[0] = 0; a[1] = 0x3100; EQ(call_ctx("sys_net_close_dump", 2, a), 0);
    s_errno = 0; EQ(call_ctx("sys_net_close_dump", 2, a), -1); OK(s_errno == 9);
    puts("sys_net offline: ok");
    return 0;
}

static int test_http(void)
{
    uint32_t cid = 0x4000;
    EQ(cellHttpClientSetUserAgent(0, 0x4100), 0x80710002);                   /* NOT_INITIALIZED */
    EQ(cellHttpInit(NULL, 0x10000), 0);
    EQ(cellHttpCreateClient((uint32_t*)(uintptr_t)cid), 0);
    uint32_t c = rd32(cid);
    EQ(cellHttpClientSetUserAgent(c + 7, gstr(0x4100, "RC/1.0")), 0x80710010);  /* BAD_CLIENT */
    EQ(cellHttpClientSetUserAgent(c, 0), 0x80710004);                         /* NO_BUFFER */
    EQ(cellHttpClientSetUserAgent(c, gstr(0x4100, "RC/1.0")), 0);
    vm_write32(0x4200, gstr(0x4300, "X-Test")); vm_write32(0x4204, gstr(0x4400, "1"));
    EQ(cellHttpClientSetHeader(c, 0x4200), 0);
    EQ(cellHttpClientSetHeader(c, 0), 0x80710004);
    vm_write32(0x4200, gstr(0x4300, "Bad:Name"));
    EQ(cellHttpClientSetHeader(c, 0x4200), 0x80710027);                       /* INVALID_VALUE */
    vm_write32(0x4500 + 4, 0);
    EQ(cellHttpClientSetProxy(c, 0x4500), 0x80710007);                        /* INVALID_URI */
    EQ(cellHttpClientSetProxy(c, 0), 0);
    EQ(cellHttpClientSetVersion(c, 1, 0), 0);
    EQ(cellHttpClientSetAutoRedirect(c, 0), 0);
    EQ(cellHttpClientSetAutoAuthentication(c, 1), 0);
    EQ(cellHttpClientSetPipeline(c, 1), 0);
    EQ(cellHttpClientSetKeepAlive(c, 0), 0);
    EQ(cellHttpClientSetCookieStatus(c, 1), 0);
    EQ(cellHttpClientSetResponseBufferMax(c, 100), 0);
    EQ(cellHttpClientSetConnTimeout(c, 5000000), 0);
    EQ(cellHttpClientSetSendTimeout(c, -1), 0x80710027);
    EQ(cellHttpClientSetRecvTimeout(c, 0x100000000LL), 0x80710027);
    EQ(cellHttpClientSetSslCallback(c, 0x5000, 0x5100), 0);
    EQ(cellHttpClientCloseAllConnections(c), 0);
    vm_write32(0x4600, 0xAAAA);
    EQ(cellHttpClientPollConnections(c, 0x4600, 0), 0x80710012);              /* NO_CONNECTION */
    OK(rd32(0x4600) == 0xAAAA);
    EQ(cellHttpClientPollConnections(c, 0, 0), 0x80710004);
    EQ(cellHttpRequestSetContentLength(31, 10), 0x80710011);                  /* BAD_TRANS */
    puts("cellHttp client settings: ok");
    return 0;
}

static int test_gameupdate(void)
{
    ps3_net_set_online(0);
    EQ(cellGameUpdateCheckStartWithoutDialogAsync(0x6000, 0x77), 0x8002cc01);
    EQ(cellGameUpdateTerm(), 0x8002cc01);
    EQ(cellGameUpdateInit(), 0);
    EQ(cellGameUpdateInit(), 0x8002cc02);
    EQ(cellGameUpdateCheckFinishAsync(0x6000, 0x77), 0x8002cc08);             /* NOT_START */
    EQ(cellGameUpdateCheckStartAsync(0x6100, 0, 0x77), 0x8002cc03);           /* INVALID_ADDR */
    s_cb_count = 0;
    EQ(cellGameUpdateCheckStartAsync(0x6100, 0x6000, 0x77), 0);
    OK(s_cb_count == 1 && s_cb_opd == 0x6000 && s_cb_args[0] == 3 && s_cb_args[1] == 0x80130182u && s_cb_args[2] == 0x77);
    EQ(cellGameUpdateCheckStartWithoutDialogAsync(0x6000, 0x77), 0x8002cc07); /* BUSY */
    EQ(cellGameUpdateCheckFinishAsync(0x6000, 0x78), 0);
    OK(s_cb_count == 2 && s_cb_args[0] == 5 && s_cb_args[1] == 0 && s_cb_args[2] == 0x78);
    ps3_net_set_online(1);
    EQ(cellGameUpdateCheckStartWithoutDialogAsync(0x6000, 0x79), 0);
    OK(s_cb_count == 3 && s_cb_args[0] == 0 && s_cb_args[1] == 0);            /* NO_UPDATE */
    ps3_net_set_online(0);
    EQ(cellGameUpdateTerm(), 0);
    puts("cellGameUpdate: ok");
    return 0;
}

static int test_ssl(void)
{
    EQ(cellSslCertGetSubjectName(0x7000, 0x7100), 0x80740001);
    EQ(cellSslInit(NULL, 0), 0);
    EQ(cellSslCertGetSubjectName(0, 0x7100), 0x80740005);                    /* INVALID_CERTIFICATE */
    EQ(cellSslCertGetIssuerName(0x7000, 0), 0x80740004);                     /* NO_BUFFER */
    EQ(cellSslCertGetIssuerName(0x7000, 0x7100), 0x80740005);
    EQ(cellSslCertGetNameEntryCount(0x7000, 0x7100), 0x80740005);
    EQ(cellSslCertGetNameEntryInfo(0x7000, 0, 0x7100, 0x7200, 0x7300, 0), 0x80740005);
    EQ(cellSslCertGetRsaPublicKeyModulus(0x7000, 0x7100, 0x7200), 0x80740005);
    EQ(cellSslCertGetRsaPublicKeyExponent(0x7000, 0, 0x7200), 0x80740004);
    puts("cellSsl certificates: ok");
    return 0;
}

static int test_rudp(void)
{
    EQ(cellRudpCreateContext(0, 0, 0x8000), 0x80770001);
    EQ(cellRudpInit(0x8100), 0);
    EQ(cellRudpInit(0x8100), 0x80770002);
    EQ(cellRudpEnableInternalIOThread(0x4000, 1000), 0);
    EQ(cellRudpEnableInternalIOThread(0x4000, 1000), 0x80770010);            /* THREAD_IN_USE */
    EQ(cellRudpSetEventHandler(0x8200, 0x8300), 0);
    EQ(cellRudpCreateContext(0x8200, 0, 0), 0x80770004);
    EQ(cellRudpCreateContext(0x8200, 0, 0x8000), 0);
    s32t ctx = (s32t)rd32(0x8000);
    OK(ctx >= 1);
    vm_write32(0x8400, 1200);
    EQ(cellRudpSetOption(ctx, 1, 0x8400, 4), 0);
    EQ(cellRudpSetOption(ctx, 12, 0x8400, 4), 0x80770005);                    /* INVALID_OPTION */
    EQ(cellRudpSetOption(ctx + 5, 1, 0x8400, 4), 0x80770003);                 /* INVALID_CONTEXT_ID */
    memset(s_mem + 0x8500, 0xEE, 64);
    EQ(cellRudpGetContextStatus(ctx, 0x8500, 64), 0);
    OK(rd32(0x8500) == 0 && rd32(0x8504) == 0);                                /* IDLE, zero-filled */
    EQ(cellRudpGetRemoteInfo(ctx, 0x8600, 0x8700, 0x8800), 0x80770011);       /* NOT_ACCEPTABLE */
    EQ(cellRudpInitiate(ctx, 0x8600, 16, 1), 0x80770013);                     /* NOT_BOUND */
    EQ(cellRudpBind(ctx, 3, 0, 0), 0x80770015);                               /* INVALID_VPORT */
    EQ(cellRudpBind(ctx, 3, 1, 2), 0x80770006);                               /* INVALID_MUXMODE */
    EQ(cellRudpBind(ctx, 3, 1, 0), 0);
    EQ(cellRudpBind(ctx, 3, 1, 0), 0x8077001d);                               /* ALREADY_BOUND */
    EQ(cellRudpInitiate(ctx, 0x8600, 16, 1), 0x8077000b);                     /* CONN_TIMEOUT: no peer */
    EQ(cellRudpGetContextStatus(ctx, 0x8500, 64), 0); OK(rd32(0x8500) == 1);  /* CLOSED */
    EQ(cellRudpWrite(ctx, 0x8900, 8, 0), 0x80770011);
    EQ(cellRudpRead(ctx, 0x8900, 8, 0, 0), 0x80770011);
    EQ(cellRudpRead(ctx, 0, 8, 0, 0), 0x80770004);
    EQ(cellRudpGetSizeReadable(ctx), 0);
    EQ(cellRudpGetSizeWritable(ctx), 0);
    EQ(cellRudpTerminate(ctx), 0);
    EQ(cellRudpTerminate(ctx), 0x80770003);
    EQ(cellRudpEnd(), 0);
    puts("cellRudp: ok");
    return 0;
}

static int test_np(void)
{
    uint32_t npid = 0x9000; memset(s_mem + npid, 0, 36); gstr(npid, "someone");
    EQ(sceNpManagerGetAvatarUrl(0x9100), 0x8002aa01);
    EQ(sceNpInit(0x100000, NULL), 0);
    EQ(sceNpManagerGetAvatarUrl(0), 0x8002aa03);
    EQ(sceNpManagerGetAvatarUrl(0x9100), 0x8002aa0c);                         /* OFFLINE */
    EQ(sceNpManagerSubSignin(1, 0, 0, 0), 0x8002aa03);
    EQ(sceNpManagerSubSignin(1, 0x9200, 0, 0), 0x8002aa0c);
    EQ(sceNpManagerSubSignout(npid), 0x8002aa06);                             /* ID_NOT_FOUND */
    s_psnr = 1;
    memset(s_mem + 0x9100, 0xEE, 128);
    EQ(sceNpManagerGetAvatarUrl(0x9100), 0); OK(s_mem[0x9100] == 0 && s_mem[0x9100 + 127] == 0);
    EQ(sceNpManagerSubSignin(1, 0x9200, 0, 0), 0x8002aa0a);                   /* INVALID_STATE */
    s_psnr = 0;

    /* Basic */
    uint32_t ev = 0x9300;
    EQ(sceNpBasicGetEvent((uint32_t*)(uintptr_t)ev, (void*)(uintptr_t)0x9310, (void*)(uintptr_t)0x9320,
                          (uint32_t*)(uintptr_t)0x9330), 0x8002a66d);           /* NOT_REGISTERED */
    EQ(sceNpBasicSendMessageGui(0x9400, 0), 0x8002a66d);
    EQ(sceNpBasicRegisterContextSensitiveHandler(NULL, (void*)(uintptr_t)0x9500, NULL), 0x8002a665);
    EQ(sceNpBasicRegisterContextSensitiveHandler((void*)(uintptr_t)0x9600, (void*)(uintptr_t)0x9500, NULL), 0);
    EQ(sceNpBasicRegisterContextSensitiveHandler((void*)(uintptr_t)0x9600, (void*)(uintptr_t)0x9500, NULL), 0x8002a66b);
    EQ(sceNpBasicGetEvent((uint32_t*)(uintptr_t)ev, NULL, (void*)(uintptr_t)0x9320, (uint32_t*)(uintptr_t)0x9330), 0x8002a665);
    EQ(sceNpBasicGetEvent((uint32_t*)(uintptr_t)ev, (void*)(uintptr_t)0x9310, (void*)(uintptr_t)0x9320,
                          (uint32_t*)(uintptr_t)0x9330), 0x8002a66a);           /* NO_EVENT */
    memset(s_mem + 0x9400, 0, 48);
    vm_write16(0x9400 + 8, 1);                                                  /* mainType GENERAL */
    EQ(sceNpBasicSendMessageGui(0x9400, 0), 0x8002a678);                      /* NOT_CONNECTED */
    vm_write16(0x9400 + 8, 9);
    EQ(sceNpBasicSendMessageGui(0x9400, 0), 0x8002a665);
    EQ(sceNpBasicAddFriend(npid, 0, 0), 0x8002a678);
    EQ(sceNpBasicAddFriend(0, 0, 0), 0x8002a665);
    s_psnr = 1;
    EQ(sceNpBasicAddFriend(npid, 0, 0), 0x8002a671);                          /* CANCEL */
    s_psnr = 0;
    EQ(sceNpBasicAddPlayersHistory(npid, 0), 0);
    EQ(sceNpBasicAddPlayersHistory(npid, gstr(0x9700, "met in a match")), 0);
    uint32_t cnt = 0x9800;
    EQ(sceNpBasicGetPlayersHistoryEntryCount(0, (uint32_t*)(uintptr_t)cnt), 0); OK(rd32(cnt) == 1);
    memset(s_mem + 0xA000, 'x', 400); s_mem[0xA000 + 400] = 0;
    EQ(sceNpBasicAddPlayersHistory(npid, 0xA000), 0x8002a66b);                /* EXCEEDS_MAX */
    EQ(sceNpBasicGetFriendPresenceByIndex(0, 0x9900, 0x9A00, 0), 0x8002aa06);
    EQ(sceNpBasicGetFriendPresenceByIndex(0, 0x9900, 0, 0), 0x8002a665);
    EQ(sceNpBasicGetFriendPresenceByNpId2(npid, 0x9A00, 0), 0x8002aa06);
    EQ(sceNpBasicGetBlockListEntry(0, 0x9900), 0x8002aa06);
    EQ(sceNpBasicRecvMessageAttachmentLoad(1, 0x9B00, 0x9C00), 0x8002a673);   /* INVALID_DATA_ID */
    EQ(sceNpBasicRecvMessageCustom(1, 0, 0), 0x8002a663);                     /* NOT_SUPPORTED */
    EQ(sceNpBasicRecvMessageCustom(3, 0x10, 0), 0x8002a665);
    EQ(sceNpBasicRecvMessageCustom(4, 1, 0), 0x8002a671);
    EQ(sceNpBasicUnregisterHandler(), 0);
    EQ(sceNpBasicRecvMessageCustom(4, 1, 0), 0x8002a66d);

    /* Signaling */
    EQ(sceNpSignalingDeactivateConnection(0, 1), 0x8002a801);
    EQ(sceNpSignalingInit(), 0);
    EQ(sceNpSignalingCreateCtx((void*)(uintptr_t)npid, NULL, NULL, (uint32_t*)(uintptr_t)0x9D00), 0);
    uint32_t sctx = rd32(0x9D00);
    EQ(sceNpSignalingDeactivateConnection(sctx + 3, 1), 0x8002a805);           /* CTX_NOT_FOUND */
    EQ(sceNpSignalingDeactivateConnection(sctx, 1), 0x8002a80e);               /* CONN_NOT_FOUND */
    EQ(sceNpSignalingTerminateConnection(sctx, 1), 0x8002a80e);

    /* Lookup */
    EQ(sceNpLookupInit(), 0);
    s32t lt = sceNpLookupCreateTitleCtx(0x9E00, npid);
    OK(lt >= 1);
    s32t ltr = sceNpLookupCreateTransactionCtx(lt);
    OK(ltr >= 1);
    EQ(sceNpLookupNpIdAsync(ltr, 0, 0x9F00, 0, 0), 0x8002a1a1);                /* INSUFFICIENT_ARGUMENT */
    EQ(sceNpLookupNpIdAsync(ltr + 9, npid, 0x9F00, 0, 0), 0x8002a1a3);         /* INVALID_ID */
    EQ(sceNpLookupNpIdAsync(ltr, npid, 0x9F00, 0, 0), 0x8002a1a4);             /* INVALID_ONLINE_ID */
    EQ(sceNpLookupTitleSmallStorageAsync(ltr, 0xB000, 0x20000, 0xB100, 0, 0), 0x8002a104);
    EQ(sceNpLookupTitleSmallStorageAsync(ltr, 0xB000, 0x100, 0xB100, 0, 0), 0x8002a1a4);
    s_psnr = 1;
    vm_write32(0xB100, 77);
    EQ(sceNpLookupTitleSmallStorageAsync(ltr, 0xB000, 0x100, 0xB100, 0, 0), 0); OK(rd32(0xB100) == 0);
    s_psnr = 0;

    /* Score: the clan rankings (context handlers) */
    np_score_register_ctx();
    OK(registered("sceNpScoreGetClansRankingByRangeAsync") && registered("sceNpScoreGetClansRankingByClanIdAsync"));
    uint64_t a[15] = { 0 };
    a[0] = 1; a[2] = 1; a[9] = 10;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0x8002a102);  /* NOT_INITIALIZED */
    EQ(sceNpScoreInit(), 0);
    s32t st = sceNpScoreCreateTitleCtx(0x9E00, 0, npid);
    s32t str = sceNpScoreCreateTransactionCtx(st);
    OK(st >= 1 && str >= 1);
    a[0] = (uint64_t)str; a[9] = 0;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0x8002a1a1);  /* arrayNum 0 */
    a[9] = 10; a[2] = 0;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0x8002a104);  /* startSerialRank 0 */
    a[2] = 1; a[13] = 0xC000;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0x8002a104);  /* option */
    a[13] = 0; a[0] = 30;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0x8002a1a3);  /* INVALID_ID */
    a[0] = (uint64_t)str;
    EQ(call_ctx("sceNpScoreGetClansRankingByRangeAsync", 14, a), 0);
    vm_write32(0xC100, 0);
    EQ(sceNpScorePollAsync(str, 0xC100), 0); OK(rd32(0xC100) == 0x8002a1a4);   /* finished: INVALID_ONLINE_ID */
    uint64_t b[15] = { 0 };
    b[0] = (uint64_t)str; b[10] = 5;
    EQ(call_ctx("sceNpScoreGetClansRankingByClanIdAsync", 15, b), 0);
    b[8] = 0x10;
    EQ(call_ctx("sceNpScoreGetClansRankingByClanIdAsync", 15, b), 0x8002a104); /* reserved2 */
    puts("sceNp manager/Basic/Signaling/Lookup/Score: ok");
    return 0;
}

static int test_matching2_commerce2(void)
{
    EQ(sceNpMatching2ClearEventData(1, 5), 0x80022303);
    EQ(sceNpMatching2Init(0x10000, 0, 0), 0);
    vm_write32(0xD000, 0); gstr(0xD000, "NPWR00000");
    EQ(sceNpMatching2CreateContext(0x9000, 0xD000, 0xD100, 0xD200, 0), 0);
    uint16_t m = vm_read16(0xD200);
    EQ(sceNpMatching2ClearEventData(m, 5), 0x80022339);                        /* EVENT_DATA_NOT_FOUND */
    EQ(sceNpMatching2ClearEventData(m + 5, 5), 0x80022306);
    EQ(sceNpMatching2GetUserInfoList(m, 0xD300, 0, 0xD400), 0x8002232d);       /* SERVER_NOT_AVAILABLE */
    EQ(sceNpMatching2GetRoomDataExternalList(m, 0xD300, 0, 0xD400), 0x8002232d);
    EQ(sceNpMatching2SetRoomMemberDataInternal(m, 0xD300, 0, 0xD400), 0x8002232d);
    EQ(sceNpMatching2GetUserInfoList(m + 5, 0xD300, 0, 0xD400), 0x80022306);

    EQ(sceNpCommerce2DoProductBrowseStartAsync(0, gstr(0xE000, "EP9000-NPEA00000_00-PRODUCT"), 0, 0), 0x80023001);
    EQ(sceNpCommerce2Init(), 0);
    EQ(sceNpCommerce2CreateCtx(1, (void*)(uintptr_t)0x9000, 0, 0, (void*)(uintptr_t)0xE100), 0);
    uint32_t cc = rd32(0xE100);
    EQ(sceNpCommerce2DoProductBrowseStartAsync(cc, 0, 0, 0), 0x80023003);
    vm_write32(0xE200, 8);
    EQ(sceNpCommerce2DoProductBrowseStartAsync(cc, 0xE000, 0, 0xE200), 0x8002301c);   /* INVALID_SIZE */
    vm_write32(0xE200, 4);
    EQ(sceNpCommerce2DoProductBrowseStartAsync(cc + 3, 0xE000, 0, 0xE200), 0x8002300c); /* CTX_NOT_FOUND */
    EQ(sceNpCommerce2DoProductBrowseStartAsync(cc, 0xE000, 0, 0xE200), 0x8002aa0c);     /* SCE_NP_ERROR_OFFLINE */
    EQ(sceNpCommerce2DoProductBrowseFinishAsync(cc), 0x8002300e);                       /* REQ_NOT_FOUND */
    puts("Matching2 and Commerce2 product browse: ok");
    return 0;
}

static int test_tus(void)
{
    uint32_t npid = 0x9000;
    np_tus_register_ctx();
    OK(registered("sceNpTusSetDataAsync") && registered("sceNpTusSetData"));
    EQ(sceNpTusCreateTitleCtx(0xF000, 0xF100, npid), 0x8002a102);
    EQ(sceNpTusInit(0), 0);
    EQ(sceNpTusInit(0), 0x8002a101);
    EQ(sceNpTusCreateTitleCtx(0, 0xF100, npid), 0x8002a1a1);
    s32t title = sceNpTusCreateTitleCtx(0xF000, 0xF100, npid);
    OK(title >= 1);
    EQ(sceNpTusCreateTransactionCtx(title), 0x8002a1a4);                       /* not signed in */
    EQ(sceNpTusCreateTransactionCtx(title + 9), 0x8002a1a4);                   /* sign-in comes first */
    EQ(sceNpTusDestroyTransactionCtx(5), 0x8002a1a3);
    EQ(sceNpTusAbortTransaction(5), 0x8002a1a3);
    EQ(sceNpTusPollAsync(5, 0xF200), 0x8002a1a3);
    /* argument checks come before the sign-in */
    vm_write32(0xF300, 1); vm_write32(0xF304, (uint32_t)-1);
    EQ(sceNpTusSetMultiSlotVariableAsync(1, npid, 0, 0xF400, 1, 0), 0x8002a1a1);
    EQ(sceNpTusSetMultiSlotVariableAsync(1, npid, 0xF300, 0xF400, 0, 0), 0x8002a104);
    EQ(sceNpTusSetMultiSlotVariableAsync(1, npid, 0xF300, 0xF400, 65, 0), 0x8002a1b1);
    EQ(sceNpTusSetMultiSlotVariableAsync(1, npid, 0xF300, 0xF400, 2, 0), 0x8002a104);   /* slot -1 */
    EQ(sceNpTusSetMultiSlotVariableAsync(1, npid, 0xF300, 0xF400, 1, 0), 0x8002a1a4);
    EQ(sceNpTusGetMultiSlotVariableAsync(1, npid, 0xF300, 0xF500, 100, 1, 0), 0x8002a1ac);   /* INVALID_ALIGNMENT */
    EQ(sceNpTusGetMultiSlotVariableAsync(1, npid, 0xF300, 0xF500, 120, 1, 0), 0x8002a1a4);
    EQ(sceNpTusGetMultiUserVariableAsync(1, npid, 0, 0xF500, 120 * 102, 102, 0), 0x8002a1ad); /* TOO_MANY_NPID */
    EQ(sceNpTusGetMultiUserVariableAsync(1, npid, -1, 0xF500, 120, 1, 0), 0x8002a104);
    EQ(sceNpTusGetMultiUserVariableAsync(1, npid, 0, 0xF500, 120, 1, 0), 0x8002a1a4);
    EQ(sceNpTusGetDataAsync(1, npid, 0, 0xF600, 400, 0, 0, 0), 0x8002a1ac);
    EQ(sceNpTusGetDataAsync(1, npid, 0, 0xF600, 488, 0, 0, 0), 0x8002a1a4);
    uint64_t a[9] = { 1, npid, 0, 16, 16, 0xF700, 0xF800, 100, 0 };
    EQ(call_ctx("sceNpTusSetDataAsync", 9, a), 0x8002a1ac);                    /* infoStructSize */
    a[7] = 392; a[8] = 0xF900; vm_write32(0xF900, 8);
    EQ(call_ctx("sceNpTusSetDataAsync", 9, a), 0x8002a1ac);                    /* option size (stack arg) */
    vm_write32(0xF900, 12);
    EQ(call_ctx("sceNpTusSetDataAsync", 9, a), 0x8002a1a4);
    /* signed in (psnr): a transaction exists; the server has no storage for the title */
    s_psnr = 1;
    s32t tr = sceNpTusCreateTransactionCtx(title);
    OK(tr >= 1);
    EQ(sceNpTusCreateTransactionCtx(title + 9), 0x8002a1a3);
    EQ(sceNpTusPollAsync(tr, 0xF200), 1);                                       /* nothing requested yet */
    EQ(sceNpTusGetMultiSlotVariableAsync(tr, npid, 0xF300, 0xF500, 120, 1, 0), 0);
    vm_write32(0xF200, 0);
    EQ(sceNpTusPollAsync(tr, 0xF200), 0); OK(rd32(0xF200) == 0x8002a4a6);      /* NO_SUCH_TITLE */
    a[0] = (uint64_t)tr;
    EQ(call_ctx("sceNpTusSetDataAsync", 9, a), 0);
    EQ(sceNpTusAbortTransaction(tr), 0);
    EQ(sceNpTusDestroyTransactionCtx(tr), 0);
    EQ(sceNpTusDestroyTransactionCtx(tr), 0x8002a1a3);
    s_psnr = 0;
    EQ(sceNpTusDestroyTitleCtx(title), 0);
    EQ(sceNpTusDestroyTitleCtx(title), 0x8002a1a3);
    EQ(sceNpTusTerm(), 0);
    EQ(sceNpTusTerm(), 0x8002a102);
    puts("sceNpTus: ok");
    return 0;
}

static int test_clans(void)
{
    EQ(sceNpClansCreateRequest(0x10000, 0), 0x80022702);
    EQ(sceNpClansTerm(), 0x80022702);
    EQ(sceNpClansInit(0, 0xF100, 0, 0, 0), 0x80022705);
    EQ(sceNpClansInit(0xF000, 0xF100, 0, 0, 1), 0x80022703);                  /* flags: NOT_SUPPORTED */
    EQ(sceNpClansInit(0xF000, 0xF100, 0, 0, 0), 0);
    EQ(sceNpClansInit(0xF000, 0xF100, 0, 0, 0), 0x80022701);
    EQ(sceNpClansCreateRequest(0, 0), 0x80022705);
    EQ(sceNpClansCreateRequest(0x10000, 1), 0x80022703);
    EQ(sceNpClansCreateRequest(0x10000, 0), 0);
    uint32_t h = rd32(0x10000);
    OK(h >= 1);
    EQ(sceNpClansCreateClan(h, 0, gstr(0x10100, "TAG"), 0x10200), 0x80022705);
    EQ(sceNpClansCreateClan(h, gstr(0x10300, "A clan"), gstr(0x10100, "TOOLONGTAG"), 0x10200), 0x80022706);
    vm_write32(0x10200, 0x5555);
    EQ(sceNpClansCreateClan(h, 0x10300, gstr(0x10100, "TAG"), 0x10200), 0x80022711);   /* SERVICE_UNAVAILABLE */
    OK(rd32(0x10200) == 0x5555);
    EQ(sceNpClansCreateClan(h + 5, 0x10300, 0x10100, 0x10200), 0x80022705);   /* unknown handle */
    EQ(sceNpClansGetClanList(h, 0, 0, 0), 0x80022705);
    vm_write32(0x10400, 0); vm_write32(0x10404, 101);
    EQ(sceNpClansGetClanList(h, 0x10400, 0x10500, 0x10600), 0x80022706);
    vm_write32(0x10404, 10);
    EQ(sceNpClansGetClanList(h, 0x10400, 0x10500, 0x10600), 0x80022711);
    EQ(sceNpClansGetClanInfo(h, 1, 0), 0x80022705);
    EQ(sceNpClansGetClanInfo(h, 1, 0x10700), 0x80022711);
    EQ(sceNpClansGetMemberList(h, 1, 0, 1, 0, 0x10600), 0x80022711);
    EQ(sceNpClansSearchByName(h, 0, 0, 0, 0x10600), 0x80022705);
    EQ(sceNpClansSearchByName(h, 0, 0x10800, 0, 0x10600), 0x80022711);
    EQ(sceNpClansLeaveClan(h, 1), 0x80022711);
    EQ(sceNpClansDisbandClan(h, 0), 0x80022705);
    EQ(sceNpClansChangeMemberRole(h, 1, 0x9000, 5), 0x80022705);
    memset(s_mem + 0x11000, 0, 1600); memset(s_mem + 0x11000, 's', 60);     /* subject longer than 54 */
    EQ(sceNpClansSendInvitation(h, 1, 0x9000, 0x11000), 0x80022706);
    EQ(sceNpClansRetrieveAnnouncements(h, 0xFFFFFFFFu, 0, 0, 0x10600), 0x80022705);
    memset(s_mem + 0x11000, 0, 1600); memcpy(s_mem + 0x11000, "hi", 2);
    EQ(sceNpClansPostAnnouncement(h, 1, 0x11000, 0, 0, 0x10900), 0x80022705); /* duration 0 */
    EQ(sceNpClansPostAnnouncement(h, 1, 0x11000, 0, 60, 0x10900), 0x80022711);
    EQ(sceNpClansPostChallenge(h, 1, 2, 0x11000, 0, 60, 0x10900), 0x80022703); /* no data */
    EQ(sceNpClansRemovePostedChallenge(h, 1, 2, 3), 0x80022711);
    EQ(sceNpClansDestroyRequest(h), 0);
    EQ(sceNpClansDestroyRequest(h), 0x80022705);
    EQ(sceNpClansTerm(), 0);
    puts("sceNpClans: ok");
    return 0;
}

int main(void)
{
    vm_base = s_mem;
    ppu_vm_size = sizeof s_mem;
    ps3_net_set_online(0);
    if (test_sys_net() || test_http() || test_gameupdate() || test_ssl() || test_rudp() || test_np()
        || test_matching2_commerce2() || test_tus() || test_clans())
        return 1;
    puts("network offline model checks passed");
    return 0;
}
