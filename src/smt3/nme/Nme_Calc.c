#include "common.h"
#include "facility.h"
#include "kernel.h"
#include "nme.h"
#include "font.h"

/* Maniax's name-entry work is 0x30 bytes shorter before the texture handles. */
#ifdef VERSION_MNAX
#define NME_TEXTURES(w) ((sdfTexHandle_t **)((u8 *)(w) + 0x5184))
#else
#define NME_TEXTURES(w) ((w)->textures)
#endif

extern nmeWork_t *nmeCalcWork;
extern void *dds3GetProcessData(dds3ProcessID_t *id);
extern s32 dds3FadeCheck(void);
extern void cmpUpdateTerminate(void *work);



/* This descriptor is in .data, outside the GP-relative range. */
extern fclSpriteTbl_t GBWK_2[4] __attribute__((section(".data")));
extern nmeWork_t *namelist_buttonAnim_end;
extern void frSetFontColor(frFontHandle_t *font, u32 color);
extern void cmpDrawBoxGDefault(s32 x, s32 y, s32 z, s32 width, s32 height,
                              s32 *colors, s32 ot);
extern void nmeDrawCursorAt(s32 x, s32 y, s32 z, s8 mode, s32 ot);
extern void fclDrawSpriteRes(s32 x, s32 y, s32 z, s32 *clip, s32 page, s32 spriteId,
                             s32 *texList, fclSpriteTbl_t *table, s32 ot);

s32 nmeCalc(dds3ProcessID_t *id)
{
    nmeCalcWork = dds3GetProcessData(id);
    if (dds3FadeCheck() != 0) {
        return 0;
    }
    if (nmeCalcWork->sequence.flag & 0x10) {
        cmpUpdateTerminate(nmeCalcWork);
        return -1;
    }
    return 0;
}

void nmeDrawCharRow(s32 x, s32 y, s32 unused)
{
    fclSpriteTbl_t *table = GBWK_2;
    s32 ot = 0x47;

    fclDrawSpriteRes(x, y, 0, 0, 3, 0x12,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x13,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x14,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
}

void nmeDrawSymbolRow(s32 x, s32 y, s32 unused)
{
    fclSpriteTbl_t *table = GBWK_2;
    s32 ot = 0x47;

    fclDrawSpriteRes(x, y, 0, 0, 3, 0x12,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x1B,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x1C,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
}

#ifdef VERSION_MNAX
/* Maniax keeps each input row as sixteen bytes (eight two-byte characters;
 * USA: sixteen characters), so the rows end 0x30 bytes earlier. */
typedef struct {
    u8 unk0[0x5151];
    u8 inputChars[3][16];
} nmeMnaxCharView_t;

extern void *memcpy(void *dest, const void *src, size_t size);
extern const s8 nmeNameTextTemplate[];
extern s32 nmeGetCursorPos(nmeWork_t *work);
extern s32 nmeGetInputMode(nmeWork_t *work);
extern frFontHandle_t *frLinkFontString(u8 *text, s8 style, s8 color, s8 type,
                                        frFontHandle_t *chain);
extern void frSetDrawPosition(frFontHandle_t *request, u32 x, u32 y);
extern void frSetPriority(frFontHandle_t *request, u32 priority);
extern s32 frDrawFontOt(frFontHandle_t *request, s8 mode, u32 ot);
extern s32 frDestroyFontRequestAfter(frFontHandle_t *request);
#endif

/* NON_MATCHING. Behaviour checked against retail: 3 PCSX2 call vectors give the same return and memory,
 * seq33, covering only the paths those calls took.
 * (sdfport 73e010f, tools/funcvec, tests/funcvec.sh). */
#ifdef NON_MATCHING
extern void *memcpy(void *dest, const void *src, size_t size);
extern const s8 nmeNameTextTemplate[];
extern s32 nmeGetCursorPos(nmeWork_t *work);
extern void cmpSetSkillListRows(s32 rows);
extern frFontHandle_t *frLinkFontString(u8 *text, s8 style, s8 color, s8 type,
                                        frFontHandle_t *chain);
extern void frSetDrawPosition(frFontHandle_t *request, u32 x, u32 y);
extern void frSetPriority(frFontHandle_t *request, u32 priority);
extern s32 frDrawFontOt(frFontHandle_t *request, s8 mode, u32 ot);
extern s32 frDestroyFontRequestAfter(frFontHandle_t *request);
extern s32 nmeGetInputMode(nmeWork_t *work);
extern u8 frDisableMode(u8 flag);
extern u8 frEnableMode(s32 flag);
#endif

#if defined(VERSION_MNAX)
void nmeDrawNameText(s32 x, s32 y, s32 secondX, s32 secondY, u32 color, s32 mode)
{
    u8 text[3];
    s32 cursor, i;
    frFontHandle_t *font;
    /* As in nmeDrawNameString: stride and priority are locals. */
    s32 step = 0x15, z = 0;

    memcpy(text, nmeNameTextTemplate, 3);
    cursor = nmeGetCursorPos(namelist_buttonAnim_end);
    nmeDrawCharRow((x - 0x25) << 4, (y - 4) << 3, 0);
    for (i = 0; i < 4; i++) {
        s32 sprite = 8;
        if (mode == 0 && nmeGetInputMode(namelist_buttonAnim_end) == 0 && cursor == i) {
            sprite = 9;
        }
        fclDrawSpriteRes((x + i * 0x15) << 4, (y + 0x16) << 3, 0, NULL, 0, sprite,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    }
    for (i = 0; i < 4; i++) {
        s32 sprite = 8;
        if (mode == 0 && nmeGetInputMode(namelist_buttonAnim_end) == 1 && cursor == i) {
            sprite = 9;
        }
        fclDrawSpriteRes((secondX + i * 0x15) << 4, (secondY + 0x16) << 3, 0, NULL, 0, sprite,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    }
    fclDrawSpriteRes((x - 0x13) << 4, (y + 9) << 3, 0, NULL, 0, 0x11,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[0]; i++) {
        text[0] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[0][i * 2];
        text[1] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[0][i * 2 + 1];
        font = frLinkFontString(text, 4, 0, 0, NULL);
        frSetDrawPosition(font, (x + i * step) << 4, y << 3);
        frSetFontColor(font, color);
        frSetPriority(font, z << 4);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
    fclDrawSpriteRes((secondX - 0x13) << 4, (secondY + 9) << 3, 0, NULL, 0, 0x12,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[1]; i++) {
        text[0] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[1][i * 2];
        text[1] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[1][i * 2 + 1];
        font = frLinkFontString(text, 4, 0, 0, NULL);
        frSetDrawPosition(font, (secondX + i * step) << 4, secondY << 3);
        frSetFontColor(font, color);
        frSetPriority(font, z << 4);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
}
#elif defined(NON_MATCHING)
void nmeDrawNameText(s32 x, s32 y, s32 secondX, s32 secondY, u32 color, s32 mode)
{
    volatile s32 first = nmeNameTextTemplate[0];
    volatile s32 second = nmeNameTextTemplate[1];
    volatile s32 terminator = nmeNameTextTemplate[2];
    u8 text[3];
    s32 cursor, i;

    text[0] = first;
    text[1] = second;
    text[2] = terminator;
    cursor = nmeGetCursorPos(namelist_buttonAnim_end);
    nmeDrawCharRow((x - 0x25) * 16, (y - 4) * 8, 0);
    for (i = 0; i < 8; i++) {
        s32 sprite = 8;
        if (mode == 0 && nmeGetInputMode(namelist_buttonAnim_end) == 0 && cursor == i) {
            sprite = 9;
        }
        fclDrawSpriteRes(x * 16 + 0xF0 + i * 0x150, (y + 0x16) * 8,
                         0, NULL, 0, sprite, (s32 *)NME_TEXTURES(namelist_buttonAnim_end),
                         GBWK_2, 0x47);
    }
    for (i = 0; i < 8; i++) {
        s32 sprite = 8;
        if (mode == 0 && nmeGetInputMode(namelist_buttonAnim_end) == 1 && cursor == i) {
            sprite = 9;
        }
        fclDrawSpriteRes(secondX * 16 + 0xF0 + i * 0x150, (secondY + 0x16) * 8,
                         0, NULL, 0, sprite, (s32 *)NME_TEXTURES(namelist_buttonAnim_end),
                         GBWK_2, 0x47);
    }
    cmpSetSkillListRows(0x18);
    fclDrawSpriteRes((x - 0x13) * 16, (y + 9) * 8, 0, NULL, 0, 0x12,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    frDisableMode(1);
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[0]; i++) {
        frFontHandle_t *font;
        text[0] = namelist_buttonAnim_end->inputChars[0][i][0];
        text[1] = namelist_buttonAnim_end->inputChars[0][i][1];
        font = frLinkFontString(text, 4, 0, 0, NULL);
        frSetDrawPosition(font, x * 16 + 0xF0 + i * 0x150, y * 8);
        frSetFontColor(font, color);
        frSetPriority(font, 0);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
    fclDrawSpriteRes((secondX - 0x13) * 16, (secondY + 9) * 8, 0, NULL, 0, 0x11,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[1]; i++) {
        frFontHandle_t *font;
        text[0] = namelist_buttonAnim_end->inputChars[1][i][0];
        text[1] = namelist_buttonAnim_end->inputChars[1][i][1];
        font = frLinkFontString(text, 4, 0, 0, NULL);
        frSetDrawPosition(font, secondX * 16 + 0xF0 + i * 0x150, secondY * 8);
        frSetFontColor(font, color);
        frSetPriority(font, 0);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
    frEnableMode(1);
    cmpSetSkillListRows(-1);
}
#else
INCLUDE_ASM(const s32, "nme/Nme_Calc", nmeDrawNameText);
#endif

#if defined(VERSION_MNAX)
void nmeDrawNameString(s32 x, s32 y, u32 color, s32 mode)
{
    u8 text[3];
    s32 cursor, i;
    frFontHandle_t *font;
    /* The character stride and priority are locals: retail computes the
     * stride's 0x12 << 4 at run time and keeps the zero priority in a
     * saved register. */
    s32 step = 0x12, z = 0;

    memcpy(text, nmeNameTextTemplate, 3);
    cursor = nmeGetCursorPos(namelist_buttonAnim_end);
    nmeDrawCharRow((x - 0x36) << 4, (y - 8) << 3, 0);
    for (i = 0; i < 8; i++) {
        s32 sprite = 0x41;
        if (mode == 0 && cursor == i) {
            sprite = 0x42;
        }
        fclDrawSpriteRes((x + i * 0x12) << 4, (y + 0x12) << 3, 0, NULL, 0, sprite,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    }
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[2]; i++) {
        text[0] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[2][i * 2];
        text[1] = ((nmeMnaxCharView_t *)namelist_buttonAnim_end)->inputChars[2][i * 2 + 1];
        font = frLinkFontString(text, 1, 0, 0, NULL);
        frSetDrawPosition(font, (x + i * step) << 4, y << 3);
        frSetFontColor(font, color);
        frSetPriority(font, z << 4);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
}
#elif defined(NON_MATCHING)
void nmeDrawNameString(s32 x, s32 y, u32 color, s32 mode)
{
    volatile s32 first = nmeNameTextTemplate[0];
    volatile s32 second = nmeNameTextTemplate[1];
    volatile s32 terminator = nmeNameTextTemplate[2];
    u8 text[3];
    s32 cursor, i;

    text[0] = first;
    text[1] = second;
    text[2] = terminator;
    cursor = nmeGetCursorPos(namelist_buttonAnim_end);
    nmeDrawSymbolRow((x - 0x18) * 16, (y - 8) * 8, 0);
    cmpSetSkillListRows(0x16);
    for (i = 0; i < 8; i++) {
        s32 sprite = 0x41;
        if (mode == 0 && cursor == i) {
            sprite = 0x42;
        }
        fclDrawSpriteRes((x + 0x10) * 16 + i * 0x120, (y + 0x12) * 8,
                         0, NULL, 0, sprite, (s32 *)NME_TEXTURES(namelist_buttonAnim_end),
                         GBWK_2, 0x47);
    }
    for (i = 0; i < namelist_buttonAnim_end->inputRowEnabled[2]; i++) {
        frFontHandle_t *font;
        text[0] = namelist_buttonAnim_end->inputChars[2][i][0];
        text[1] = namelist_buttonAnim_end->inputChars[2][i][1];
        font = frLinkFontString(text, 1, 0, 0, NULL);
        frSetDrawPosition(font, (x + 0x10) * 16 + i * 0x120, y * 8);
        frSetFontColor(font, color);
        frSetPriority(font, 0);
        frDrawFontOt(font, 0, 0x47);
        frDestroyFontRequestAfter(font);
    }
    cmpSetSkillListRows(-1);
}
#else
INCLUDE_ASM(const s32, "nme/Nme_Calc", nmeDrawNameString);
#endif

void nmeDrawEditIcons(void)
{
    s32 colors[4];
    fclSpriteTbl_t *table = GBWK_2;

    colors[0] = 0x80808079;
    colors[1] = 0x80808079;
    colors[2] = 0x80808079;
    colors[3] = 0x80808079;

    fclDrawSpriteRes(0, 0, 0x10, colors, 3, 0x19,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, 0x47);
    fclDrawSpriteRes(0, 0, 0x10, colors, 3, 0x1A,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, 0x47);
}

void nmeDrawTextBox(s32 x, s32 y, s32 z)
{
    s32 colors[4];

    colors[0] = 0x45202680;
    colors[1] = 0;
    colors[2] = 0x45202680;
    colors[3] = 0;
    cmpDrawBoxGDefault(x, y, z, 0x4F, 0x16, colors, 0x47);
}

typedef struct {
    const char *question;
    const char *yes;
    const char *no;
} nmeColoredTextStrings_t;

extern const nmeColoredTextStrings_t nmeColoredText;
extern void cmpSetSkillListRows(s32 rows);
extern frFontHandle_t *frwGetSmallFontFRQ2(s32 x, s32 y, s32 priority, u32 color,
                                             const char *text, frFontHandle_t *previous);
extern void frSetDrawPosition(frFontHandle_t *request, u32 x, u32 y);
extern void frSetPriority(frFontHandle_t *request, u32 priority);
extern void frSetFontColorNo(frFontHandle_t *request, u8 colorNo);
extern s32 frDrawFontOt(frFontHandle_t *request, s8 mode, u32 ot);
extern s32 frDestroyFontRequestAfter(frFontHandle_t *request);
extern u8 frDisableMode(u8 flag);

#ifdef VERSION_MNAX
extern frFontHandle_t *frLinkFontString(u8 *text, s8 style, s8 color, s8 type, frFontHandle_t *chain);

/* Maniax: this table is the first of the unit's .rodata, ahead of the
 * Maniax-only tab table, so it is defined here. */
extern const char nmeConfirmQuestion[], nmeConfirmYes[], nmeConfirmNo[];
const nmeColoredTextStrings_t nmeColoredText = {nmeConfirmQuestion, nmeConfirmYes, nmeConfirmNo};

void nmeDrawColoredText(s8 selected)
{
    nmeColoredTextStrings_t text = nmeColoredText;
    frFontHandle_t *request;

    request = frLinkFontString((u8 *)text.question, 0, 0, 0, NULL);
    frSetDrawPosition(request, 0x970, 0x808);
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);

    request = frLinkFontString((u8 *)text.yes, 0, 0, 0, NULL);
    frSetDrawPosition(request, 0x9D0, 0x910);
    if (selected == 0) {
        frSetFontColorNo(request, 1);
        nmeDrawTextBox(0x9A0, 0x920, 0xFF);
    }
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);

    request = frLinkFontString((u8 *)text.no, 0, 0, 0, NULL);
    frSetDrawPosition(request, 0x9D0, 0x9E8);
    if (selected == 1) {
        frSetFontColorNo(request, 1);
        nmeDrawTextBox(0x9A0, 0x9F0, 0xFF);
    }
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);
}
#else
void nmeDrawColoredText(s8 selected)
{
    nmeColoredTextStrings_t text = nmeColoredText;
    frFontHandle_t *request;

    cmpSetSkillListRows(0x18);
    request = frwGetSmallFontFRQ2(0x97, 0x101, 0xFF, 0x80808080, text.question, NULL);
    frSetDrawPosition(request, 0x970, 0x808);
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);

    request = frwGetSmallFontFRQ2(0x9D, 0x122, 0xFF, 0x80808080, text.yes, NULL);
    frSetDrawPosition(request, 0x9D0, 0x910);
    if (selected == 0) {
        frSetFontColorNo(request, 1);
        nmeDrawTextBox(0x9A0, 0x920, 0xFF);
    }
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);

    request = frwGetSmallFontFRQ2(0x9D, 0x13D, 0xFF, 0x80808080, text.no, NULL);
    frSetDrawPosition(request, 0x9D0, 0x9E8);
    if (selected == 1) {
        frSetFontColorNo(request, 1);
        nmeDrawTextBox(0x9A0, 0x9F0, 0xFF);
    }
    frSetPriority(request, 0xFF0);
    frDrawFontOt(request, 0, 0x47);
    frDestroyFontRequestAfter(request);

    frDisableMode(1);
    cmpSetSkillListRows(-1);
}
#endif

extern void nmeDrawNameText(s32 x, s32 y, s32 width, s32 height, u32 color, s32 mode);
extern void nmeDrawNameString(s32 x, s32 y, u32 color, s32 mode);
extern void nmeDrawEditIcons(void);
extern void nmeDrawColoredText(s8 color);

#ifdef VERSION_MNAX
void nmeDrawButtonGuide(void)
{
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x17,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    nmeDrawNameText(0x9B, 0x64, 0x10B, 0x64, 0xFFD19280, 1);
    if (namelist_buttonAnim_end->entryMode == 0) {
        fclDrawSpriteRes(0, 0, 0, 0, 3, 0x18,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
        nmeDrawNameString(0xAC, 0x9A, 0xFFD19280, 1);
    }
    nmeDrawEditIcons();
    nmeDrawColoredText((s8)((u8)namelist_buttonAnim_end->wrapCursor.unk_22 +
                            (u8)namelist_buttonAnim_end->wrapCursor.unk_24));
}
#else
void nmeDrawButtonGuide(void)
{
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x17,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x1D,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    nmeDrawNameText(0x2A, 0x64, 0xF4, 0x64, 0xFFD19280, 1);
    if (namelist_buttonAnim_end->entryMode == 0) {
        fclDrawSpriteRes(0, 0, 0, 0, 3, 0x18,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
        nmeDrawNameString(0x83, 0x9A, 0xFFD19280, 1);
    }
    nmeDrawEditIcons();
    nmeDrawColoredText((s8)((u8)namelist_buttonAnim_end->wrapCursor.unk_22 +
                            (u8)namelist_buttonAnim_end->wrapCursor.unk_24));
}
#endif

void nmeDrawPanelSprites(void)
{
    s32 colors[4];

    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x5,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x6,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x7,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x8,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0x9,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0xA,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0xB,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 0xC,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);

    colors[0] = 0x80808053;
    colors[1] = 0x80808053;
    colors[2] = 0x80808053;
    colors[3] = 0x80808053;

    fclDrawSpriteRes(0, 0, 0, colors, 3, 0xD,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
    fclDrawSpriteRes(0, 0, 0, colors, 3, 0xE,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
}

#ifdef VERSION_MNAX
void nmeDrawNamePanel(void)
{
    fclSpriteTbl_t *table = GBWK_2;
    s32 ot = 0x47;

    fclDrawSpriteRes(0, 0, 0, 0, 3, 0,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 1,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 2,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 0, 4,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 0, 5,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 0, 0xB,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 0, 0x10,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
}
#else
void nmeDrawNamePanel(void)
{
    fclSpriteTbl_t *table = GBWK_2;
    s32 ot = 0x47;

    fclDrawSpriteRes(0, 0, 0, 0, 3, 0,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 1,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(0, 0, 0, 0, 3, 2,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
}
#endif

#ifdef VERSION_MNAX
/* Maniax-only: the four input-mode tabs. The selected tab draws its highlight
 * sprite; the others draw their frame and, for the kana/symbol modes, a label
 * (dimmed on the last tab in mode 2). */
void mnax_00298268(s8 mode, u8 inputMode)
{
    s32 color = 0x40404080;
    u8 sprites[4][3] = {
        { 0x13, 0x00, 0x0C },
        { 0x14, 0x01, 0x0D },
        { 0x15, 0x02, 0x0E },
        { 0x16, 0x03, 0x0F },
    };
    s32 i;

    for (i = 0; i < 4; i++) {
        if (i == mode) {
            fclDrawSpriteRes(0, 0, 0, 0, 0, sprites[i][0],
                             (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
            continue;
        }
        fclDrawSpriteRes(0, 0, 0, 0, 0, sprites[i][1],
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
        switch (inputMode) {
        case 0:
        case 1:
            fclDrawSpriteRes(0, 0, 0, 0, 0, sprites[i][2],
                             (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
            break;
        case 2:
            if (i != 3) {
                fclDrawSpriteRes(0, 0, 0, 0, 0, sprites[i][2],
                                 (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
            } else {
                fclDrawSpriteRes(0, 0, 0, &color, 0, sprites[i][2],
                                 (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
            }
            break;
        }
    }
}

/* Maniax-only: tab cursor positions (x, y) for the four input modes, copied
 * by the tab cursor function at 0x298DA8 (still assembly), which names this
 * table. Its .rodata follows the tab table above. */
const s32 mnax_00546A90[4][2] = {
    { 0x29, 0xDA },
    { 0x29, 0xDA },
    { 0x29, 0xDA },
    { 0x4B, 0xDA },
};
#endif

void nmeDrawCharCell(s32 x, s32 y, u16 index, s8 row, frFontHandle_t *font)
{
    nmeWork_t *work = namelist_buttonAnim_end;
    s32 drawX = x;
    s32 drawY = y;
    s32 modeSelection;
    s32 spriteId;

    if ((s8)work->unk1B != 0) {
        goto done;
    }
    if (work->primaryCursorActive != 0) {
        goto done;
    }
    if (work->alternateCursorActive != 0) {
        goto done;
    }
    modeSelection = work->modeCursor.unk_22 + (u8)work->modeCursor.unk_24;
    spriteId = (modeSelection == 3) ? 0x48 : 0x47;
    if (index != work->inputCursor.unk_22 + (u8)work->inputCursor.unk_24) {
        goto done;
    }
    if (row != work->nameCursor.unk_22 + (u8)work->nameCursor.unk_24) {
        goto done;
    }
    fclDrawSpriteRes(drawX << 4, drawY << 3, 0, 0, 0, spriteId,
                     (s32 *)NME_TEXTURES(work), GBWK_2, 0x47);
    frSetFontColor(font, 0xFFD19280);
done:
    return;
}

#ifdef NON_MATCHING
extern const s8 nmeCursorSkip[];
extern const s8 nmeCharOffsets[][2];
extern const u8 *nmeCharTables[];

void nmeDrawCursorAt(s32 x, s32 y, s32 z, s8 mode, s32 ot)
{
    u8 text[3];
    s8 skip[3];
    s32 rows, step, row, column;

    memcpy(text, nmeNameTextTemplate, 3);
    memcpy(skip, nmeCursorSkip, 3);
    rows = (s8)namelist_buttonAnim_end->inputCursor.unk_26;
    step = 0x16;
    if (mode == 2) {
        step = 0x17;
    }
    cmpSetSkillListRows(0x18);
    for (row = 0; row < rows; row++) {
        s32 drawX = 0;
        s32 drawY = row * 0x1E;
        for (column = 0; column < 16 - skip[mode]; column++) {
            s32 index = row * 16 + column + skip[mode];
            s8 offsetX = nmeCharOffsets[index][0];
            s8 offsetY = nmeCharOffsets[index][1];
            const u8 *character;
            frFontHandle_t *font;

            fclDrawSpriteRes(drawX * 16, drawY * 8, 0, NULL, 0, 0x2B,
                             (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
            character = nmeCharTables[mode] + index * 2;
            text[0] = character[0];
            text[1] = character[1];
            font = frLinkFontString(text, 4, 0, 0, NULL);
            frSetDrawPosition(font, (x + drawX + offsetX) * 16, (y + drawY + offsetY) * 8);
            frSetPriority(font, z * 16);
            nmeDrawCharCell(drawX, drawY, row, column, font);
            drawX += step;
            frDrawFontOt(font, 0, 0x47);
            frDestroyFontRequestAfter(font);
        }
    }
    cmpSetSkillListRows(-1);
}
#else
INCLUDE_ASM(const s32, "nme/Nme_Calc", nmeDrawCursorAt);
#endif

void nmeDrawCursor(void)
{
    nmeDrawCursorAt(0x29, 0xDA, 0, 0, 0x47);
}

void nmeDrawTabSprite(s32 x, s32 y, s32 unused)
{
    fclSpriteTbl_t *table = GBWK_2;
    s32 ot = 0x47;

    fclDrawSpriteRes(x, y, 0, 0, 3, 0xF,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x10,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
    fclDrawSpriteRes(x, y, 0, 0, 3, 0x11,
                     (s32 *)NME_TEXTURES(namelist_buttonAnim_end), table, ot);
}

void nmeDrawModeTabs(s8 mode)
{
    nmeDrawTabSprite(0x540, 0x3D8, 0);
    switch (mode) {
    case 0:
    case 1:
        fclDrawSpriteRes(0, 0, 0, 0, 3, 0x15,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
        break;
    case 2:
        fclDrawSpriteRes(0, 0, 0, 0, 3, 0x16,
                         (s32 *)NME_TEXTURES(namelist_buttonAnim_end), GBWK_2, 0x47);
        break;
    default:
        break;
    }
}

#ifdef VERSION_MNAX
const char nmeAutoMemoryLabel[16] = "\201\273\202\231\201\331\201\373\201\345\202\231\201\331";

const char nmeMenuMemoryLabel[24] = "\201\274\202\231\201\316\201\374\212\256\207\232\201\214\207\275\213\373";

const char nmeVibrationLabel[24] = "\201\341\201\265\201\347\201\375\202\231\201\310\201\370\202\204";
#else
extern const char nmeConfirmQuestion[], nmeConfirmYes[], nmeConfirmNo[];
const nmeColoredTextStrings_t nmeColoredText = {nmeConfirmQuestion, nmeConfirmYes, nmeConfirmNo};

const char nmeAutoMemoryLabel[16] = "AUTO MEMORY";

const char nmeMenuMemoryLabel[16] = "MENU MEMORY";

const char nmeVibrationLabel[16] = "VIBRATION";
#endif

