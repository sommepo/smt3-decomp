#include "common.h"
#include "facility.h"
#include "fpu.h"
#include "libsdf.h"
#ifdef VERSION_MNAX
/* Maniax: the saved game header inside the work is 4 bytes shorter, so every
 * later field moves down by 4. */
#include "sdf_resource.h"
#include "field.h"
typedef struct
{
    void *unk_00;
    sdfMemHandle_t *unk_04;
    sdfMemHandle_t *unk_08;
    struct {
        u8 verstr[3];
        u8 ver;
        u8 TmnlMode;
        u8 TmnlId;
        u8 Lv;
        u8 mode;
        s32 PlayTime;
        union { struct { u16 endcnt; u16 newfile; }; s32 endCounterWord; };
        s32 work[4];
        u8 autoMapOpen;
        u8 autoMapOpenEX[15];
    } fileInfo;
    s32 unk_40;
    s32 unk_44;
    s32 unk_48;
    s32 unk_4C;
    s32 unk_50;
    s32 cursor;
    void *unk_58;
    void *unk_5C;
    s32 unk_60;
    s32 tover[5];
    u8 Zalpha_cnt;
    u8 reserved79[3];
    u32 Z_RGBA;
    u32 ZNalpha_cnt;
    u32 ZN_RGBA;
    s8 unk_88;
    u8 unk_89[3];
    s32 unk_8C;
    s32 unk_90;
    s32 unk_94[5];
    s32 unk_A8;
    u32 cur_alpha;
    u32 cur_alphacnt;
} mselGlobalWork_t;
#else
#include "mselGlobal.h"
#endif
#include "newdata.h"
#include "ps2asm.h"
#include "event.h"
#include "kernel.h"
#include "field.h"

extern s32 dds3KillProcessName(char *name, s32 shutdowntimer);

extern s32 fldWmapEnabled;

extern dds3ProcessID_t *mselShutDown(u32 mode);

extern const char mselProcName[];

extern void mselFreeResources(s32 arg);



extern mselGlobalWork_t *gGlobal;

s32 fclDestroyMessage(void);
s8 fclInitMessage(void *data);

s32 fclLoadTexture(char *pFileName);

extern void evtBitOn(s32 no);

extern void (*DrawPhaseTable[])(fclEncyc_instance_tag *work);

extern void *sdfMemGetPtr(sdfMemHandle_t *handle);
extern void *memset(void *ptr, s32 value, size_t size);
extern void *sdfChipAllocZeroDirect(s32 size);
extern void sdfChipFreeDirect(void *ptr);
extern u32 dds3GetRandIntA(void *state, u32 limit);
void mselCountSaves(void);
void Msel_TOverData_Clear(void);
void mselFindSaveSlot(s32 *first, s32 *second, u32 start, u32 end, s8 reverse);

void mselDevilChoice(void)
{
    mselSaveData_t *data = sdfMemGetPtr(gGlobal->unk_08);
    s32 count = data->count;
    s32 selected[10];
    s32 scores[10];
    s32 chosen[5];
    s32 i, j, remaining, chosenCount, target, index, poolCount;
    s32 *pool;

    memset(selected, -1, sizeof(selected));
    memset(scores, 0, sizeof(scores));
    memset(chosen, -1, sizeof(chosen));
    for (i = 0; i < count; i++) {
        u32 exp = data->records[i].exp;
        for (j = 0; j < 10; j++) {
            if (exp >= (u32)scores[j]) {
                mselFindSaveSlot(scores, selected, j + 1, 10, 0);
                scores[j] = exp;
                selected[j] = i;
                break;
            }
        }
    }
    remaining = 0;
    for (i = 0; i < 10; i++) {
        if (selected[i] >= 0) {
            remaining++;
        }
    }
    mselCountSaves();
    Msel_TOverData_Clear();
    chosenCount = 0;
    target = 5;
    for (i = 0; remaining > 0 && i < 3; i++) {
        index = dds3GetRandIntA(NULL, remaining);
        remaining--;
        gGlobal->tover[chosenCount] = selected[index];
        gGlobal->unk_60++;
        chosen[chosenCount++] = selected[index];
        mselFindSaveSlot(scores, selected, index + 1, 10, 1);
    }
    {
        s32 freshCount = data->count;
        if (freshCount < 5) target = freshCount;
    }
    poolCount = count - chosenCount;
    if (poolCount > 0) {
        pool = sdfChipAllocZeroDirect(poolCount * sizeof(*pool));
        if (chosenCount < target) {
            s32 filled = 0;
            for (i = 0; i < count; i++) {
                for (j = 0; j < chosenCount; j++) {
                    if (i == chosen[j]) {
                        break;
                    }
                }
                if (j >= chosenCount && j != 0) {
                    pool[filled++] = i;
                }
            }
        }
        remaining = poolCount;
        for (i = chosenCount; i < target; i++) {
            index = dds3GetRandIntA(NULL, poolCount);
            poolCount--;
            gGlobal->tover[i] = pool[index];
            gGlobal->unk_60++;
            chosen[i] = pool[index];
            mselFindSaveSlot(pool, NULL, index + 1, remaining, 1);
        }
        sdfChipFreeDirect(pool);
    }
    gGlobal->Z_RGBA = 0x64618700;
    gGlobal->Zalpha_cnt = 0;
}


void Msel_TOverData_Clear(void)
{
    s32 i;

    for (i = 0; i < 5; i++) {
        gGlobal->tover[i] = -1;
    }
    gGlobal->unk_60 = 0;
}

void mselCountSaves(void)
{
    mselGlobalWork_t *work;
    s32 i;

    for (i = 0; i < 5; i++)
    {
        gGlobal->unk_94[i] = gGlobal->tover[i];
    }
    work = gGlobal;
    work->unk_90 = work->unk_60;
    if (work->unk_60 != 0)
    {
        work->unk_88 = 0;
        work->unk_8C = 0x64618780;
    }
}

void mselFindSaveSlot(s32 *first, s32 *second, u32 start, u32 end, s8 reverse)
{
    u32 i;

    if (reverse != 0) {
        i = start;
        if (start < end) {
            for (; i < end; i++) {
                if (first != NULL) {
                    first[i - 1] = first[i];
                }
                if (second != NULL) {
                    second[i - 1] = second[i];
                }
            }
        }
    } else {
        i = end - 1;
        if (i >= start) {
            do {
                if (first != NULL) {
                    first[i] = first[i - 1];
                }
                if (second != NULL) {
                    second[i] = second[i - 1];
                }
                i--;
            } while (i >= start);
        }
    }
}

extern const char mselBgTexPath[];

extern const char mselDevilSelectTexPath[];

void mselLoadBackground(void)
{
    gGlobal->unk_40 = fclLoadTexture(mselBgTexPath);
    gGlobal->unk_44 = 0;
    gGlobal->unk_48 = fclLoadTexture(mselDevilSelectTexPath);
}

extern void sdfTexFreeDelayed(s32 aHandle);

void mselFreeTextures(void)
{
    if (gGlobal->unk_40 != 0) {
        sdfTexFreeDelayed(gGlobal->unk_40);
        gGlobal->unk_40 = 0;
    }
    if (gGlobal->unk_44 != 0) {
        sdfTexFreeDelayed(gGlobal->unk_44);
        gGlobal->unk_44 = 0;
    }
    if (gGlobal->unk_48 != 0) {
        sdfTexFreeDelayed(gGlobal->unk_48);
        gGlobal->unk_48 = 0;
    }
}

const char mselBgTexPath[20] = "/title/mslct_bg.tmx";

const char mselDevilSelectTexPath[24] = "/title/select_devil.tmx";

const char mselDataFilePath[] = "/title/mseldata.bmd";

extern sdfMemHandle_t *sdfMemAllocDirect(s32 size);
extern void *sdfMemLock(sdfMemHandle_t *handle);
extern dds3ProcessID_t *dds3StartProcess(char *name, u32 priority, s32 startTimer, s32 shutdownTimer,
                                         dds3ProcessFunc_t processFunc, dds3ProcessFunc_t shutdownFunc, void *work);
extern s32 mselMain(dds3ProcessID_t *id);
extern u32 dds3BackColor;

dds3ProcessID_t *mselShutDown(u32 mode)
{
    sdfMemHandle_t *handle;
    u32 *work;
    s32 i;
    void *fileHandle;

    handle = sdfMemAllocDirect(sizeof(mselGlobalWork_t));
    work = sdfMemLock(handle);
    gGlobal = (mselGlobalWork_t *)work;
    gGlobal->unk_00 = handle;
    gGlobal->unk_4C = 0;
    gGlobal->unk_50 = 0;
    gGlobal->cursor = 0;
    for (i = 0; i < 5; i++) {
        gGlobal->tover[i] = -1;
    }
    gGlobal->unk_60 = 0;
    mselLoadBackground();
    gGlobal->unk_A8 = mode;
    gGlobal->unk_04 = sdfMemAllocDirect(sizeof(dds3GlobalWork_t));
    gGlobal->unk_08 = sdfMemAllocDirect(0x7E90);
    fileHandle = sdfDevLoadFileEx(mselDataFilePath, (u32 *)&gGlobal->unk_5C, NULL);
    gGlobal->unk_58 = fileHandle;
    fclInitMessage(gGlobal->unk_5C);
    dds3BackColor = 0x80000000;
    return dds3StartProcess((char *)mselProcName, 0x2B19, 0, 0, mselMain, NULL, NULL);
}

extern void mselFreeTextures(void);

extern void sdfMemFreeDelayedDirect(void *aHandle);

void mselFreeResources(s32 arg)
{
    mselFreeTextures();
    fclDestroyMessage();
    sdfMemFreeDelayedDirect(gGlobal->unk_04);
    sdfMemFreeDelayedDirect(gGlobal->unk_08);
    sdfMemFreeDelayedDirect(gGlobal->unk_58);
    sdfMemFreeDelayedDirect(gGlobal->unk_00);
    gGlobal = NULL;
}

const char mselProcName[] = "mselProc";

extern void *memcpy(void *dest, const void *src, size_t size);
extern s8 dds3PadButtonState[];
extern void rsetOtOfs(s32 offset);
extern void rsetAlpha(s32 alpha);
extern void rsetTest(s32 ate, s32 atst, s32 aref, s32 afail, s32 date, s32 datm, s32 zte, s32 ztst);
extern void DrwSprite2D(s32 x, s32 y, s32 w, s32 h, s32 u, s32 v, s32 uw, s32 vh,
                        u32 color, sdfTexHandle_t *texture);
extern void dds3FadeOut(s32 r, s32 g, s32 b, s32 frames);
extern void dds3FadeIn(s32 r, s32 g, s32 b, s32 frames);
extern s32 dds3FadeCheck(void);
extern void dds3AdminiCallSequence(s32 sequence, void *data, u32 size, s32 mode);
extern s32 evtBitOff(s32 no);
extern s32 fclChkMessage(void);
extern s32 fclChkSelMessage(void);
extern s8 fclGetSelMessagePos(void);
extern void fclSetSelMessagePos(s8 pos);
extern s32 fclStartMessage(s32 message);
extern s32 fclStartSelMessage(s32 message);
extern s8 fclEndMessage(void);
extern void fclUpdateMessage(void);
extern s32 fclEncycRegist2(fclencyceelem_t *record, s32 mode);
extern void fmProcTrigSequence(s32 sequence);
extern s32 fmProcessChk(void);
extern u32 slGetExitMode(void);
extern s32 p2sdLoadCheckMidi(s32 id);
extern void p2sdLoadSMGFile(s32 id);
extern void p2sdPlayBGMFade(s32 id);
extern s32 p2sdStopBGM(s32 id);
extern void p2sdPlaySE(s32 id, s32 volume, s32 pan);
extern void mselDrawTitleAnim(void);
extern void mselDrawTitleAnim2(s32 animate);
extern void mselDrawBackground(void);
extern void mselUpdateCursor(void);
extern void mselStartDataSet(void);
extern void mselStartGame(void);

s32 mselMain(dds3ProcessID_t *id)
{
    s32 selection, count, digit, hours, seconds, i, sequence;
    mselSaveData_t *save;
    dds3GlobalWork_t *snapshot;
    sdfMemHandle_t *handle;
    mselGlobalWork_t *work;

    rsetOtOfs(0x48);
    rsetAlpha(0);
    rsetTest(1, 0, 0x80, 3, 0, 0, 1, 1);
    switch (gGlobal->unk_4C) {
    case 0:
        dds3FadeOut(0, 0, 0, 1);
        if (p2sdLoadCheckMidi(0x710000) != 0) {
            gGlobal->unk_4C = 2;
            gGlobal->unk_50 = 0;
        } else {
            p2sdLoadSMGFile(0x710000);
            gGlobal->unk_50 = 0;
            gGlobal->unk_4C = (s32)((u32)gGlobal->unk_4C + 1);
        }
        break;
    case 1:
        if (p2sdLoadCheckMidi(0x710000) != 0) {
            gGlobal->unk_50 = 0;
            gGlobal->unk_4C = (s32)((u32)gGlobal->unk_4C + 1);
        }
        break;
    case 2:
        p2sdPlayBGMFade(0x710001);
        dds3FadeIn(0, 0, 0, 20);
        gGlobal->unk_50 = 0;
        gGlobal->unk_4C = (s32)((u32)gGlobal->unk_4C + 1);
        mselStartDataSet();
        break;
    case 3:
        mselDrawTitleAnim();
        if (dds3FadeCheck() == 0) {
            /* Keep the live cursor alias in a2 until its field read. Ordinary
             * allocation merges it with the mode/store base; this local EE
             * constraint preserves native scheduling, not original spelling. */
            FAKEMATCH_ASM_REG(mselGlobalWork_t *, cursorWork, "$6");
            s32 titleMode;
            mselUpdateCursor();
            titleMode = gGlobal->unk_A8;
            cursorWork = gGlobal;
            if (titleMode == 0 && dds3PadButtonState[0x23] < 0) {
                gGlobal->unk_50 = 0;
                gGlobal->unk_4C = 22;
                dds3FadeOut(0, 0, 0, 20);
                p2sdStopBGM(0x710001);
                p2sdPlaySE(10, 127, 63);
            } else if (dds3PadButtonState[0x21] < 0 || dds3PadButtonState[0x2C] < 0) {
                if (cursorWork->cursor == 0) {
                    fldWmapEnabled = 0;
                    evtBitOn(0xAF2);
                } else {
                    fldWmapEnabled = 1;
                    evtBitOff(0xAF2);
                }
                p2sdPlaySE(8, 127, 63);
#ifdef VERSION_MNAX
                gGlobal->unk_50 = 0;
                if (gGlobal->unk_A8 != 0) {
                    Msel_TOverData_Clear();
                    p2sdStopBGM(0x710001);
                    gGlobal->unk_4C = 16;
                    dds3FadeOut(0, 0, 0, 20);
                } else {
                    gGlobal->unk_4C = 15;
                    dds3FadeOut(0, 0, 0, 20);
                }
#else
                gGlobal->unk_A8 = 1;
                gGlobal->unk_50 = 0;
                Msel_TOverData_Clear();
                p2sdStopBGM(0x710001);
                gGlobal->unk_4C = 16;
                dds3FadeOut(0, 0, 0, 20);
#endif
            }
        }
        break;
    case 15:
        mselDrawTitleAnim();
        goto startMessage;
    case 16:
        mselDrawTitleAnim();
        goto loadAttention;
    case 20:
        mselDrawTitleAnim2(0);
        /* fall through */
    case 17:
    startMessage:
        if (dds3FadeCheck() == 0) {
            dds3FadeIn(0, 0, 0, 20);
            fclStartMessage(0);
            gGlobal->unk_4C = 4;
        }
        break;
    case 21:
        mselDrawTitleAnim2(1);
        if (dds3FadeCheck() == 0) {
            gGlobal->unk_4C = 12;
            gGlobal->unk_50 = 0;
            dds3FadeIn(0, 0, 0, 90);
        }
        break;
    case 4:
        mselDrawBackground();
        if (fclChkMessage() == 0) {
            gGlobal->unk_4C = 5;
            gGlobal->unk_50 = 0;
            fclEndMessage();
            fclSetSelMessagePos(0);
            fclStartMessage(1);
            fclStartSelMessage(2);
        } else fclUpdateMessage();
        break;
    case 5:
        mselDrawBackground();
        if (fclChkSelMessage() != 0) {
            gGlobal->unk_4C = 6;
            gGlobal->unk_50 = 0;
        }
        fclUpdateMessage();
        break;
    case 6:
        mselDrawBackground();
        if (fclChkSelMessage() == 0) {
            selection = fclGetSelMessagePos();
            fclEndMessage();
            Msel_TOverData_Clear();
            if (selection == 0) {
                snapshot = sdfMemGetPtr(gGlobal->unk_04);
                *snapshot = *DDS3_GBWK;
                gGlobal->unk_4C = 18;
                gGlobal->unk_50 = 0;
                dds3FadeOut(0, 0, 0, 20);
            } else {
                p2sdStopBGM(0x710001);
                gGlobal->unk_4C = 19;
                dds3FadeOut(0, 0, 0, 20);
            }
        } else fclUpdateMessage();
        break;
    case 18:
        mselDrawBackground();
        if (dds3FadeCheck() == 0) {
            dds3FadeIn(0, 0, 0, 20);
            gGlobal->unk_4C = 7;
            gGlobal->unk_50 = 0;
            fmProcTrigSequence(4);
        }
        break;
    case 19:
        mselDrawBackground();
    loadAttention:
        if (dds3FadeCheck() == 0) {
            if (gGlobal->unk_40 != 0) {
                sdfTexFreeDelayed(gGlobal->unk_40);
                gGlobal->unk_40 = 0;
            }
            selection = fclLoadTexture("/title/att_font.tmx");
            gGlobal->unk_4C = 13;
            gGlobal->unk_44 = selection;
            gGlobal->unk_50 = 0;
            dds3FadeIn(0, 0, 0, 90);
        }
        break;
    case 7:
        if (fmProcessChk() == 0) {
            u32 exitMode = slGetExitMode();
            if (exitMode != 0) {
                mselSaveData_t *loadedSave;
                if (exitMode != 2) break;
                loadedSave = sdfMemGetPtr(gGlobal->unk_08);
                *loadedSave = DDS3_GBWK->encyclopedia;
                memcpy(&gGlobal->fileInfo, DDS3_GBWK, sizeof(gGlobal->fileInfo));
                handle = DDS3_GBWK->memHandle;
                snapshot = sdfMemGetPtr(gGlobal->unk_04);
                *DDS3_GBWK = *snapshot;
                DDS3_GBWK->memHandle = handle;
                mselDevilChoice();
                if (gGlobal->unk_60 == 0) {
                    dds3FadeIn(0, 0, 0, 90);
                    gGlobal->unk_4C = 12;
                    gGlobal->unk_50 = 0;
                } else {
                    dds3FadeIn(0, 0, 0, 15);
                    gGlobal->unk_4C = 8;
                    gGlobal->unk_50 = 0;
                    fclStartMessage(3);
                }
            } else {
                gGlobal->unk_4C = 17;
                gGlobal->unk_50 = 0;
                dds3FadeOut(0, 0, 0, 0);
            }
        }
        break;
    case 8:
        mselDrawTitleAnim2(0);
        if (fclChkMessage() == 0) {
            gGlobal->unk_4C = 9;
            gGlobal->unk_50 = 0;
            fclSetSelMessagePos(0);
            fclStartMessage(4);
            fclStartSelMessage(5);
        } else fclUpdateMessage();
        break;
    case 9:
        mselDrawTitleAnim2(0);
        if (fclChkSelMessage() != 0) {
            gGlobal->unk_4C = 10;
            gGlobal->unk_50 = 0;
        }
        fclUpdateMessage();
        break;
    case 10:
        mselDrawTitleAnim2(0);
        if (fclChkSelMessage() == 0) {
            selection = fclGetSelMessagePos();
            fclEndMessage();
            if (selection == 0) {
                gGlobal->unk_4C = 11;
                gGlobal->unk_50 = 0;
                fclStartMessage(6);
            } else if (selection == 1) {
                gGlobal->unk_4C = 8;
                gGlobal->unk_50 = 0;
                mselDevilChoice();
                fclStartMessage(3);
            } else {
                gGlobal->unk_4C = 20;
                gGlobal->unk_50 = 0;
                dds3FadeOut(0, 0, 0, 20);
            }
        } else fclUpdateMessage();
        break;
    case 11:
        mselDrawTitleAnim2(1);
        if (fclChkMessage() != 0) fclUpdateMessage();
        else {
            fclEndMessage();
            gGlobal->unk_4C = 21;
            gGlobal->unk_50 = 0;
            dds3FadeOut(0, 0, 0, 20);
        }
        break;
    case 12:
        evtBitOn(0x8B1);
        count = (s16)gGlobal->fileInfo.endcnt + 1;
        if (count <= 0) count = 1;
        if (count > 0) evtBitOn(0x8B6);
        if (count >= 2) evtBitOn(0x8B7);
        if (count >= 3) evtBitOn(0x8B8);
        if (count >= 4) evtBitOn(0x8B9);
        if (count >= 5) evtBitOn(0x8BA);
        count = gGlobal->fileInfo.PlayTime;
        if (count > 0x066FEBF8) count = 0x066FEBF8;
        /* The retail residue retains the divisors of a full h/m/s split.
         * As in the live save-preview family, compute that split before the
         * minute digit; ee-gcc removes the unused hours/seconds arithmetic
         * but retains divisor setup from its division guards. The live count
         * scalar is reused across completion-count, tick and digit phases. */
        hours = count / 108000;
        digit = (count % 108000) / 1800;
        seconds = (count % 1800) / 30;
        count = digit % 10;
        if (count == 0) evtBitOn(0x8B2);
        if (count == 8) evtBitOn(0x8B3);
        if (count == 4) evtBitOn(0x8B4);
        if (count == 5) evtBitOn(0x8B5);
        save = sdfMemGetPtr(gGlobal->unk_08);
        for (i = 0; i < 5; i++) {
            s32 entry = gGlobal->tover[i];
            if (entry != -1) fclEncycRegist2(&save->records[entry], 1);
        }
        p2sdStopBGM(0x710001);
        {
            /* Local v0 allocation only covers the pre-call handle reads.
             * Reload gGlobal after free; this is not a cross-call cache. */
            FAKEMATCH_ASM_REG(mselGlobalWork_t *, textureWork, "$2") = gGlobal;
            if (textureWork->unk_40 != 0) {
                sdfTexFreeDelayed(textureWork->unk_40);
                gGlobal->unk_40 = 0;
            }
        }
        selection = fclLoadTexture("/title/att_font.tmx");
        gGlobal->unk_4C = 13;
        gGlobal->unk_44 = selection;
        gGlobal->unk_50 = 0;
        break;
    case 13: {
        s32 fadeCounter, opacity, nextCounter;
        rsetOtOfs(0x47);
        rsetAlpha(0);
        rsetTest(1, 0, 0x80, 3, 0, 0, 1, 1);
        fadeCounter = gGlobal->unk_50;
        opacity = 128;
        if ((u32)fadeCounter - 240 < 91) {
            /* Preserve the live 128 endpoint in a0 for subtraction and clamp;
             * ordinary allocation reuses opacity and schedules div differently.
             * This target-specific aid emits no arithmetic instructions. */
            FAKEMATCH_ASM_REG(s32, maximum, "$4") = 128;
            opacity = maximum - (fadeCounter - 240) * 128 / 90;
            if (opacity < 0) opacity = 0;
            if (opacity > 128) opacity = maximum;
        }
        if (fadeCounter >= 330) opacity = 0;
        DrwSprite2D(0, 0, 0x200, 0x1C0, 0, 0, 0x200, 0x1C0,
                    ((u32)opacity << 24) | 0x808080, EE_ADDR_AS(sdfTexHandle_t *, gGlobal->unk_44));
        if (gGlobal->unk_50 == 330) dds3FadeOut(255, 255, 255, 60);
        work = gGlobal;
        nextCounter = work->unk_50;
        if (nextCounter >= 390) {
            work->unk_50 = 0;
            work->unk_4C = 14;
            mselStartGame();
        } else work->unk_50 = (s32)((u32)nextCounter + 1);
        break;
    }
    case 22: {
        s32 exitCounter;
        mselDrawTitleAnim();
        exitCounter = gGlobal->unk_50;
        if (exitCounter >= 21) {
            gGlobal->unk_50 = 0;
            gGlobal->unk_4C = 14;
            sequence = 3;
            dds3AdminiCallSequence(2, &sequence, sizeof(sequence), 0);
        } else gGlobal->unk_50 = (s32)((u32)exitCounter + 1);
        break;
    }
    }
    return 0;
}
s32 mselExit(u32 mode)
{
    mselShutDown(mode);
    return -1;
}

s32 mselInit(void)
{
    mselFreeResources(0);
    dds3KillProcessName((char *)mselProcName, 1);
    return 0;
}

s32 fldWmapIsEnabled(void)
{
    return fldWmapEnabled;
}

extern void dds3GlobalWorkUserInit2(s32 mode);

extern s32 dds3ConfigChanged(void);

extern void dds3ConfigSaveBackup(void);

extern void dds3ConfigRestoreBackup(void);

extern void evtBitOn(s32 no);

#ifdef NON_MATCHING
extern void dds3CreateGameData(s32 no);
#endif

void mselStartDataSet(void)
{
    if (gGlobal->unk_A8 == 0) {
        dds3CreateGameData(0);
    }
    if (DDS3_GBWK->transition != 0) {
        dds3GlobalWorkUserInit2(1);
    } else {
        dds3GlobalWorkUserInit2(0);
    }
    if (dds3ConfigChanged() == 0) {
        dds3ConfigSaveBackup();
    } else {
        dds3ConfigRestoreBackup();
    }
    evtBitOn(0xC0F);
}

extern fldDanteGunState_t fldRunDanteGunFlag;

extern void fldSetNextField(s32 a, s32 b);

extern void dds3AdminiCallSequence(s32 sequence, void *data, u32 datasize, s32 restore);

void mselStartGame(void)
{
    s32 field;

    fldRunDanteGunFlag.enabled = 1;
    field = 0x259;
    fldRunDanteGunFlag.counter = 0;
    fldSetNextField(0xB, 0x259);
    dds3AdminiCallSequence(7, &field, 4, 0);
}

#ifdef VERSION_MNAX
const char mnax_0053B980[28] = "Presented by ATLUS CO.,LTD.";

const char mnax_0053B9A0[11] = "Cozy Okada";

const char mnax_0053B9B0[13] = "Naoya Harano";

const char mnax_0053B9C0[19] = "Executive Producer";

const char mnax_0053B9D8[18] = "Hideyuki Yokoyama";

const char mnax_0053B9F0[11] = "Supervisor";

const char mnax_0053BA00[15] = "Nobuyuki Okude";

const char mnax_0053BA10[16] = "General Manager";

const char mnax_0053BA20[23] = "R&D1 NEW Project Staff";

const char mnax_0053BA38[17] = "Atsushi Watanabe";

const char mnax_0053BA50[19] = "Aki Ooba (Pikuko) ";

const char mnax_0053BA68[14] = "Yoko Nakamura";

const char mnax_0053BA78[15] = "Special Thanks";

const char mnax_0053BA88[11] = "AVANT INC.";

const char mnax_0053BA98[15] = "SHIROGUMI INC.";

const char mnax_0053BAA8[24] = "DIGITAL MEDIA LAB.,INC.";

const char mnax_0053BAC0[14] = "Yuji Sakamoto";

const char mnax_0053BAD0[17] = "Takuya Matsumoto";

const char mnax_0053BAE8[15] = "Tatsuya Yabuta";

const char mnax_0053BAF8[10] = "Corporate";

const char mnax_0053BB08[14] = "Sound Creator";

const char mnax_0053BB18[13] = "Kohei Magome";

const char mnax_0053BB28[15] = "Akihiro Yokota";

const char mnax_0053BB38[17] = "Yoshiko Miyahara";

const char mnax_0053BB50[17] = "Mitsuru Sasamori";

const char mnax_0053BB68[14] = "Takumi Mizuno";

const char mnax_0053BB78[14] = "Katsumi Miura";

const char mnax_0053BB88[11] = "Keiko Kato";

const char mnax_0053BB98[20] = "Yoshinori Yoshimoto";

const char mnax_0053BBB0[11] = "Yusuke Eto";

const char mnax_0053BBC0[14] = "Shin Miyahara";

const char mnax_0053BBD0[13] = "Daisuke Kuki";

const char mnax_0053BBE0[14] = "Yukari Yokoro";

const char mnax_0053BBF0[18] = "Shigenori Soejima";

const char mnax_0053BC08[9] = "Designer";

const char mnax_0053BC18[14] = "Yasuaki Nagai";

const char mnax_0053BC28[14] = "Masaya Takano";

const char mnax_0053BC38[20] = "Michishito Ishizuka";

const char mnax_0053BC50[11] = "Programmer";

const char mnax_0053BC60[16] = "Tomohiro Hosoya";

const char mnax_0053BC70[11] = "Yosuke Uda";

const char mnax_0053BC80[17] = "Tatsuya Watanabe";

const char mnax_0053BC98[16] = "Shinya Korenaga";

const char mnax_0053BCA8[9] = "Aki Ooba";

const char mnax_0053BCB8[16] = "Yuichiro Tanaka";

const char mnax_0053BCC8[11] = "Azusa Kido";

const char mnax_0053BCD8[15] = "Daisuke Kanada";

const char mnax_0053BCE8[24] = "(kimura nakaji office) ";

const char mnax_0053BD00[14] = "Nakaji Kimura";

const char mnax_0053BD10[16] = "Katsura Hashino";

const char mnax_0053BD20[15] = "Original Staff";

const char mnax_0053BD30[13] = "Kaori Takano";

const char mnax_0053BD40[15] = "Takenobu Ishii";

const char mnax_0053BD50[10] = "Yuji Goto";

const char mnax_0053BD60[16] = "Ikuya Kobayashi";

const char mnax_0053BD70[13] = "Makoto Nishi";

const char mnax_0053BD80[13] = "Akio Koshiba";

const char mnax_0053BD90[17] = "Daisuke Hasegawa";

const char mnax_0053BDA8[15] = "Kouji Yamamoto";

const char mnax_0053BDB8[16] = "Masashi Komatsu";

const char mnax_0053BDC8[13] = "Noriko Tabei";

const char mnax_0053BDD8[15] = "Rintaro Murata";

const char mnax_0053BDE8[15] = "Kazuhiro Yamao";

const char mnax_0053BDF8[16] = "Daizo Shimamura";

const char mnax_0053BE08[10] = "Promotion";

const char mnax_0053BE18[15] = "Kyoko Yasumura";

const char mnax_0053BE28[16] = "Masato Ichimiya";

const char mnax_0053BE38[14] = "Satoshi Koike";

const char mnax_0053BE48[16] = "Eiichiro Iizuka";

const char mnax_0053BE58[10] = "Marketing";

const char mnax_0053BE68[13] = "Jun Yamamoto";

const char mnax_0053BE78[20] = "CS Division Manager";

const char mnax_0053BE90[16] = "Syunsuke Aikawa";

const char mnax_0053BEA0[15] = "Humihito Ishii";

const char mnax_0053BEB0[16] = "Masao Nagashima";

const char mnax_0053BEC0[9] = "Art Work";

const char mnax_0053BED0[19] = "Takafumi Fuchigami";

const char mnax_0053BEE8[18] = "STUDIO BRACE INC.";

const char mnax_0053BF00[17] = "Manual Corporate";

const char mnax_0053BF18[17] = "Gensuke Fujisawa";

const char mnax_0053BF30[18] = "Masayoshi Akasaka";

const char mnax_0053BF48[16] = "Masaki Sakadume";

const char mnax_0053BF58[18] = "Takahiro Kawabata";

const char mnax_0053BF70[12] = "Kengo Honjo";

const char mnax_0053BF80[23] = "DIGITAL HEARTS CO.,LTD";

const char mnax_0053BF98[16] = "Debug Corporate";

const char mnax_0053BFA8[16] = "Masaru Watanabe";

const char mnax_0053BFB8[16] = "Masahide Takaya";

const char mnax_0053BFC8[9] = "Debugger";

const char mnax_0053BFD8[13] = "Kenichi Goto";

const char mnax_0053BFE8[15] = "Chief Debugger";

const char mnax_0053BFF8[14] = "Debug Manager";

const char mnax_0053C008[16] = "Hirohito Shindo";

const char mnax_0053C018[16] = "Shinjo Kanakogi";

const char mnax_0053C028[14] = "Naoto Hiraoka";

const char mnax_0053C038[19] = "Production Manager";

const char mnax_0053C050[23] = "INTERNATIONAL LIMITED.";

const char mnax_0053C068[16] = "FONTWORKS JAPAN";

const char mnax_0053C078[22] = "Font Design Corporate";

const char mnax_0053C090[16] = "Bingo Morihashi";

const char mnax_0053C0A0[16] = "Hakuei Yamamoto";

const char mnax_0053C0B0[14] = "Hiroyuki Nara";

const char mnax_0053C0C0[12] = "Ikeno Daigo";

const char mnax_0053C0D0[15] = "Hideaki Itsuno";

const char mnax_0053C0E0[16] = "Tsuyoshi Tanaka";

const char mnax_0053C0F0[9] = "Producer";

const char mnax_0053C100[42] = "(C) CAPCOM CO., LTD. ALL RIGHTS RESERVED.";

const char mnax_0053C130[14] = "R&D cooperate";

const char mnax_0053C140[17] = "Kenichi Tsuchiya";

const char mnax_0053C158[15] = "Toshiko Tasaki";

const char mnax_0053C168[25] = "Compose & Sound Creation";

const char mnax_0053C188[15] = "Masayoshi Suto";

const char mnax_0053C198[19] = "Interface Designer";

const char mnax_0053C1B0[17] = "Masakatsu Suzuki";

const char mnax_0053C1C8[16] = "Effect Designer";

const char mnax_0053C1D8[11] = "Akiko Koto";

const char mnax_0053C1E8[25] = "Battle & Effect Designer";

const char mnax_0053C208[17] = "Yasuhiro Shibuya";

const char mnax_0053C220[13] = "Yuki Inagaki";

const char mnax_0053C230[15] = "Shuji Fukasawa";

const char mnax_0053C240[13] = "Aki Sonoyama";

const char mnax_0053C250[15] = "Field Designer";

const char mnax_0053C260[13] = "Masayuki Doi";

const char mnax_0053C270[22] = "Field Design Director";

const char mnax_0053C288[17] = "Megumi Shiraishi";

const char mnax_0053C2A0[20] = "Devil CG Supervisor";

const char mnax_0053C2B8[15] = "Yoichi Yoshida";

const char mnax_0053C2C8[12] = "Taku Aoyagi";

const char mnax_0053C2D8[12] = "Kaori Okita";

const char mnax_0053C2E8[23] = "Devil Graphic Designer";

const char mnax_0053C300[18] = "Devil CG Director";

const char mnax_0053C318[18] = "Kokichi Takahashi";

const char mnax_0053C330[12] = "Mumon Usuda";

const char mnax_0053C340[12] = "Naoya Maeda";

const char mnax_0053C350[14] = "Kazuhisa Wada";

const char mnax_0053C360[15] = "Event Designer";

const char mnax_0053C370[21] = "Event Scene Director";

const char mnax_0053C388[14] = "Masayuki Higo";

const char mnax_0053C398[15] = "Nobuyoshi Miwa";

const char mnax_0053C3A8[19] = "Support Programmar";

const char mnax_0053C3C0[14] = "Takaaki Ikeda";

const char mnax_0053C3D0[21] = "Interface Programmer";

const char mnax_0053C3E8[16] = "Toshiyuki Niida";

const char mnax_0053C3F8[18] = "Effect Programmer";

const char mnax_0053C410[16] = "Main Programmer";

const char mnax_0053C420[17] = "Shuetsu Kadowaki";

const char mnax_0053C438[10] = "Daiki Ito";

const char mnax_0053C448[11] = "Sawao Kato";

const char mnax_0053C458[14] = "Field Planner";

const char mnax_0053C468[15] = "Mitsuru Hirata";

const char mnax_0053C478[15] = "System Planner";

const char mnax_0053C488[14] = "Event Planner";

const char mnax_0053C498[13] = "Shoji Meguro";

const char mnax_0053C4A8[14] = "Main Composer";

const char mnax_0053C4B8[14] = "Makoto Kitano";

const char mnax_0053C4C8[18] = "Design Supervisor";

const char mnax_0053C4E0[12] = "Eiji Ishida";

const char mnax_0053C4F0[15] = "Chief Designer";

const char mnax_0053C500[15] = "Satoshi Ooyama";

const char mnax_0053C510[17] = "Chief Programmer";

const char mnax_0053C528[14] = "Shigeo Komori";

const char mnax_0053C538[13] = "Shogo Isogai";

const char mnax_0053C548[16] = "Scenario Writer";

const char mnax_0053C558[15] = "Kazuyuki Yamai";

const char mnax_0053C568[9] = "Director";

const char mnax_0053C578[14] = "Kazuma Kaneko";

const char mnax_0053C588[18] = "Creative Director";

const char mnax_0053C5A0[21] = "/staff/stf_pic15.tmx";

const char mnax_0053C5B8[21] = "/staff/stf_pic14.tmx";

const char mnax_0053C5D0[21] = "/staff/stf_pic13.tmx";

const char mnax_0053C5E8[21] = "/staff/stf_pic12.tmx";

const char mnax_0053C600[21] = "/staff/stf_pic11.tmx";

const char mnax_0053C618[21] = "/staff/stf_pic10.tmx";

const char mnax_0053C630[21] = "/staff/stf_pic09.tmx";

const char mnax_0053C648[21] = "/staff/stf_pic08.tmx";

const char mnax_0053C660[21] = "/staff/stf_pic07.tmx";

const char mnax_0053C678[21] = "/staff/stf_pic06.tmx";

const char mnax_0053C690[21] = "/staff/stf_pic05.tmx";

const char mnax_0053C6A8[21] = "/staff/stf_pic04.tmx";

const char mnax_0053C6C0[21] = "/staff/stf_pic03.tmx";

const char mnax_0053C6D8[21] = "/staff/stf_pic02.tmx";

const char mnax_0053C6F0[21] = "/staff/stf_pic01.tmx";

const char mnax_0053C708[22] = "/staff/stf_mpic15.tmx";

const char mnax_0053C720[22] = "/staff/stf_mpic14.tmx";

const char mnax_0053C738[22] = "/staff/stf_mpic13.tmx";

const char mnax_0053C750[22] = "/staff/stf_mpic12.tmx";

const char mnax_0053C768[22] = "/staff/stf_mpic11.tmx";

const char mnax_0053C780[22] = "/staff/stf_mpic10.tmx";

const char mnax_0053C798[22] = "/staff/stf_mpic09.tmx";

const char mnax_0053C7B0[22] = "/staff/stf_mpic08.tmx";

const char mnax_0053C7C8[22] = "/staff/stf_mpic07.tmx";

const char mnax_0053C7E0[22] = "/staff/stf_mpic06.tmx";

const char mnax_0053C7F8[22] = "/staff/stf_mpic05.tmx";

const char mnax_0053C810[22] = "/staff/stf_mpic04.tmx";

const char mnax_0053C828[22] = "/staff/stf_mpic03.tmx";

const char mnax_0053C840[22] = "/staff/stf_mpic02.tmx";

const char mnax_0053C858[22] = "/staff/stf_mpic01.tmx";
#else
const char D_0053DF90[19] = "Presented by Atlus";

const char D_0053DFA8[14] = "Jesse Farrell";

const char D_0053DFB8[10] = "Carl Chen";

const char D_0053DFC8[13] = "Wayne Carter";

const char D_0053DFD8[21] = "Aron Brent Gutierrez";

const char D_0053DFF0[17] = "Assistant Editor";

const char D_0053E008[18] = "William Alexander";

const char D_0053E020[13] = "Chief Editor";

const char D_0053E030[13] = "James Kuroki";

const char D_0053E040[11] = "Translator";

const char D_0053E050[15] = "Gail Salamanca";

const char D_0053E060[18] = "Marketing Manager";

const char D_0053E078[16] = "Hiroyuki Tanaka";

const char D_0053E088[20] = "Project Coordinator";

const char D_0053E0A0[9] = "Yu Namba";

const char D_0053E0B0[15] = "Project Leader";

const char D_0053E0C0[13] = "Sonoko Saito";

const char D_0053E0D0[16] = "Product Manager";

const char D_0053E0E0[18] = "U.S. Localization";

const char D_0053E0F8[11] = "Cozy Okada";

const char D_0053E108[13] = "Naoya Harano";

const char D_0053E118[19] = "Executive Producer";

const char D_0053E130[18] = "Hideyuki Yokoyama";

const char D_0053E148[11] = "Supervisor";

const char D_0053E158[15] = "Nobuyuki Okude";

const char D_0053E168[16] = "General Manager";

const char D_0053E178[23] = "R&D1 NEW Project Staff";

const char D_0053E190[17] = "Atsushi Watanabe";

const char D_0053E1A8[19] = "Aki Ooba (Pikuko) ";

const char D_0053E1C0[14] = "Yoko Nakamura";

const char D_0053E1D0[15] = "Special Thanks";

const char D_0053E1E0[11] = "AVANT INC.";

const char D_0053E1F0[15] = "SHIROGUMI INC.";

const char D_0053E200[24] = "DIGITAL MEDIA LAB.,INC.";

const char D_0053E218[14] = "Yuji Sakamoto";

const char D_0053E228[17] = "Takuya Matsumoto";

const char D_0053E240[15] = "Tatsuya Yabuta";

const char D_0053E250[10] = "Corporate";

const char D_0053E260[14] = "Sound Creator";

const char D_0053E270[13] = "Kohei Magome";

const char D_0053E280[15] = "Akihiro Yokota";

const char D_0053E290[17] = "Yoshiko Miyahara";

const char D_0053E2A8[17] = "Mitsuru Sasamori";

const char D_0053E2C0[14] = "Takumi Mizuno";

const char D_0053E2D0[14] = "Katsumi Miura";

const char D_0053E2E0[11] = "Keiko Kato";

const char D_0053E2F0[20] = "Yoshinori Yoshimoto";

const char D_0053E308[11] = "Yusuke Eto";

const char D_0053E318[14] = "Shin Miyahara";

const char D_0053E328[13] = "Daisuke Kuki";

const char D_0053E338[14] = "Yukari Yokoro";

const char D_0053E348[18] = "Shigenori Soejima";

const char D_0053E360[9] = "Designer";

const char D_0053E370[14] = "Yasuaki Nagai";

const char D_0053E380[14] = "Masaya Takano";

const char D_0053E390[20] = "Michishito Ishizuka";

const char D_0053E3A8[11] = "Programmer";

const char D_0053E3B8[16] = "Tomohiro Hosoya";

const char D_0053E3C8[11] = "Yosuke Uda";

const char D_0053E3D8[17] = "Tatsuya Watanabe";

const char D_0053E3F0[16] = "Shinya Korenaga";

const char D_0053E400[9] = "Aki Ooba";

const char D_0053E410[16] = "Yuichiro Tanaka";

const char D_0053E420[11] = "Azusa Kido";

const char D_0053E430[15] = "Daisuke Kanada";

const char D_0053E440[24] = "(kimura nakaji office) ";

const char D_0053E458[14] = "Nakaji Kimura";

const char D_0053E468[16] = "Katsura Hashino";

const char D_0053E478[15] = "Original Staff";

const char D_0053E488[13] = "Kaori Takano";

const char D_0053E498[15] = "Takenobu Ishii";

const char D_0053E4A8[10] = "Yuji Goto";

const char D_0053E4B8[16] = "Ikuya Kobayashi";

const char D_0053E4C8[13] = "Makoto Nishi";

const char D_0053E4D8[13] = "Akio Koshiba";

const char D_0053E4E8[17] = "Daisuke Hasegawa";

const char D_0053E500[15] = "Kouji Yamamoto";

const char D_0053E510[16] = "Masashi Komatsu";

const char D_0053E520[13] = "Noriko Tabei";

const char D_0053E530[15] = "Rintaro Murata";

const char D_0053E540[15] = "Kazuhiro Yamao";

const char D_0053E550[16] = "Daizo Shimamura";

const char D_0053E560[10] = "Promotion";

const char D_0053E570[15] = "Kyoko Yasumura";

const char D_0053E580[16] = "Masato Ichimiya";

const char D_0053E590[14] = "Satoshi Koike";

const char D_0053E5A0[16] = "Eiichiro Iizuka";

const char D_0053E5B0[10] = "Marketing";

const char D_0053E5C0[13] = "Jun Yamamoto";

const char D_0053E5D0[20] = "CS Division Manager";

const char D_0053E5E8[16] = "Syunsuke Aikawa";

const char D_0053E5F8[15] = "Humihito Ishii";

const char D_0053E608[16] = "Masao Nagashima";

const char D_0053E618[19] = "Takafumi Fuchigami";

const char D_0053E630[18] = "STUDIO BRACE INC.";

const char D_0053E648[17] = "Manual Corporate";

const char D_0053E660[17] = "Gensuke Fujisawa";

const char D_0053E678[18] = "Masayoshi Akasaka";

const char D_0053E690[16] = "Masaki Sakadume";

const char D_0053E6A0[18] = "Takahiro Kawabata";

const char D_0053E6B8[12] = "Kengo Honjo";

const char D_0053E6C8[23] = "DIGITAL HEARTS CO.,LTD";

const char D_0053E6E0[16] = "Debug Corporate";

const char D_0053E6F0[16] = "Masaru Watanabe";

const char D_0053E700[16] = "Masahide Takaya";

const char D_0053E710[9] = "Debugger";

const char D_0053E720[13] = "Kenichi Goto";

const char D_0053E730[15] = "Chief Debugger";

const char D_0053E740[14] = "Debug Manager";

const char D_0053E750[16] = "Hirohito Shindo";

const char D_0053E760[16] = "Shinjo Kanakogi";

const char D_0053E770[14] = "Naoto Hiraoka";

const char D_0053E780[19] = "Production Manager";

const char D_0053E798[23] = "INTERNATIONAL LIMITED.";

const char D_0053E7B0[16] = "FONTWORKS JAPAN";

const char D_0053E7C0[22] = "Font Design Corporate";

const char D_0053E7D8[16] = "Bingo Morihashi";

const char D_0053E7E8[16] = "Hakuei Yamamoto";

const char D_0053E7F8[14] = "Hiroyuki Nara";

const char D_0053E808[12] = "Ikeno Daigo";

const char D_0053E818[15] = "Hideaki Itsuno";

const char D_0053E828[16] = "Tsuyoshi Tanaka";

const char D_0053E838[9] = "Producer";

const char D_0053E848[42] = "(C) CAPCOM CO., LTD. ALL RIGHTS RESERVED.";

const char D_0053E878[24] = "R&D in cooperation with";

const char D_0053E890[17] = "Kenichi Tsuchiya";

const char D_0053E8A8[15] = "Toshiko Tasaki";

const char D_0053E8B8[20] = "Composition & Sound";

const char D_0053E8D0[15] = "Masayoshi Suto";

const char D_0053E8E0[19] = "Interface Designer";

const char D_0053E8F8[17] = "Masakatsu Suzuki";

const char D_0053E910[16] = "Effect Designer";

const char D_0053E920[11] = "Akiko Koto";

const char D_0053E930[25] = "Battle & Effect Designer";

const char D_0053E950[17] = "Yasuhiro Shibuya";

const char D_0053E968[13] = "Yuki Inagaki";

const char D_0053E978[15] = "Shuji Fukasawa";

const char D_0053E988[13] = "Aki Sonoyama";

const char D_0053E998[15] = "Field Designer";

const char D_0053E9A8[13] = "Masayuki Doi";

const char D_0053E9B8[22] = "Field Design Director";

const char D_0053E9D0[17] = "Megumi Shiraishi";

const char D_0053E9E8[20] = "Devil CG Supervisor";

const char D_0053EA00[15] = "Yoichi Yoshida";

const char D_0053EA10[12] = "Taku Aoyagi";

const char D_0053EA20[12] = "Kaori Okita";

const char D_0053EA30[23] = "Devil Graphic Designer";

const char D_0053EA48[18] = "Devil CG Director";

const char D_0053EA60[18] = "Kokichi Takahashi";

const char D_0053EA78[12] = "Mumon Usuda";

const char D_0053EA88[12] = "Naoya Maeda";

const char D_0053EA98[14] = "Kazuhisa Wada";

const char D_0053EAA8[15] = "Event Designer";

const char D_0053EAB8[21] = "Event Scene Director";

const char D_0053EAD0[14] = "Masayuki Higo";

const char D_0053EAE0[15] = "Nobuyoshi Miwa";

const char D_0053EAF0[19] = "Support Programmar";

const char D_0053EB08[14] = "Takaaki Ikeda";

const char D_0053EB18[21] = "Interface Programmer";

const char D_0053EB30[16] = "Toshiyuki Niida";

const char D_0053EB40[18] = "Effect Programmer";

const char D_0053EB58[16] = "Main Programmer";

const char D_0053EB68[17] = "Shuetsu Kadowaki";

const char D_0053EB80[10] = "Daiki Ito";

const char D_0053EB90[11] = "Sawao Kato";

const char D_0053EBA0[14] = "Field Planner";

const char D_0053EBB0[15] = "Mitsuru Hirata";

const char D_0053EBC0[15] = "System Planner";

const char D_0053EBD0[14] = "Event Planner";

const char D_0053EBE0[13] = "Shoji Meguro";

const char D_0053EBF0[14] = "Main Composer";

const char D_0053EC00[14] = "Makoto Kitano";

const char D_0053EC10[18] = "Design Supervisor";

const char D_0053EC28[12] = "Eiji Ishida";

const char D_0053EC38[15] = "Chief Designer";

const char D_0053EC48[15] = "Satoshi Ooyama";

const char D_0053EC58[17] = "Chief Programmer";

const char D_0053EC70[14] = "Shigeo Komori";

const char D_0053EC80[13] = "Shogo Isogai";

const char D_0053EC90[16] = "Scenario Writer";

const char D_0053ECA0[15] = "Kazuyuki Yamai";

const char D_0053ECB0[9] = "Director";

const char D_0053ECC0[14] = "Kazuma Kaneko";

const char D_0053ECD0[18] = "Creative Director";

const char D_0053ECE8[21] = "/staff/stf_pic15.tmx";

const char D_0053ED00[21] = "/staff/stf_pic14.tmx";

const char D_0053ED18[21] = "/staff/stf_pic13.tmx";

const char D_0053ED30[21] = "/staff/stf_pic12.tmx";

const char D_0053ED48[21] = "/staff/stf_pic11.tmx";

const char D_0053ED60[21] = "/staff/stf_pic10.tmx";

const char D_0053ED78[21] = "/staff/stf_pic09.tmx";

const char D_0053ED90[21] = "/staff/stf_pic08.tmx";

const char D_0053EDA8[21] = "/staff/stf_pic07.tmx";

const char D_0053EDC0[21] = "/staff/stf_pic06.tmx";

const char D_0053EDD8[21] = "/staff/stf_pic05.tmx";

const char D_0053EDF0[21] = "/staff/stf_pic04.tmx";

const char D_0053EE08[21] = "/staff/stf_pic03.tmx";

const char D_0053EE20[21] = "/staff/stf_pic02.tmx";

const char D_0053EE38[21] = "/staff/stf_pic01.tmx";

const char D_0053EE50[22] = "/staff/stf_mpic15.tmx";

const char D_0053EE68[22] = "/staff/stf_mpic14.tmx";

const char D_0053EE80[22] = "/staff/stf_mpic13.tmx";

const char D_0053EE98[22] = "/staff/stf_mpic12.tmx";

const char D_0053EEB0[22] = "/staff/stf_mpic11.tmx";

const char D_0053EEC8[22] = "/staff/stf_mpic10.tmx";

const char D_0053EEE0[22] = "/staff/stf_mpic09.tmx";

const char D_0053EEF8[22] = "/staff/stf_mpic08.tmx";

const char D_0053EF10[22] = "/staff/stf_mpic07.tmx";

const char D_0053EF28[22] = "/staff/stf_mpic06.tmx";

const char D_0053EF40[22] = "/staff/stf_mpic05.tmx";

const char D_0053EF58[22] = "/staff/stf_mpic04.tmx";

const char D_0053EF70[22] = "/staff/stf_mpic03.tmx";

const char D_0053EF88[22] = "/staff/stf_mpic02.tmx";

const char D_0053EFA0[22] = "/staff/stf_mpic01.tmx";

#endif
