// DIORAMA: the board is a clay tower standing on a small island in a still sea, in a soft sunlit miniature
// world. One place, one material, one light: the music moves the light (time of day), the wind and the water;
// the game moves clay.
//
// Layout contract: the camera is a long lens, slightly elevated and turned, framing the whole island; the well
// stays near the layout's rectangle, and while a change of universe runs the camera glides to the layout's front
// view (same pixels for the well as every other universe) and back.
#include "universes/diorama/diorama.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/diorama/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Comfortaa Bold (SIL Open Font License, see OFL-Comfortaa.txt beside this file).
const unsigned char FONT_TTF[] = {
#include "universes/diorama/Comfortaa-Bold.ttf.inc"
};
namespace shaders = diorama_shaders;
stbtt_fontinfo gFont;

using ugl::U;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compileStage(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s %s shader error:\n%s\n", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return s;
}

vec3 hex(uint32_t c) {
    return {srgbToLinear(((c >> 16) & 255) / 255.f), srgbToLinear(((c >> 8) & 255) / 255.f), srgbToLinear((c & 255) / 255.f)};
}
vec3 hexS(uint32_t c) { return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f}; } // sRGB as is

mat4 inverse(const mat4& m) {
    const float* a = m.m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    mat4 r{};
    if (std::fabs(det) < 1e-12f) return mat4::identity();
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
    return r;
}

vec4 quatAxis(vec3 axis, float angle) {
    axis = normalize(axis);
    float s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

// ---------------------------------------------------------------------------------------------------------
// Art direction: a fixed set of seven clay colors, three landscapes, and the light of five moments.

// Palette slots of the static world.
enum Slot { S_GRASS, S_GRASS2, S_SAND, S_CLIFF, S_ROCK, S_TRUNK, S_LEAF, S_LEAF2, S_TOWER, S_TOWER2, S_STELE, S_WALL,
            S_HILL1, S_HILL2, S_HILL3, S_LANTERN };
// Prop shapes (meshes built in buildWorld, in this order).
enum PropKind { P_ROUND, P_CYPRESS, P_PINE, P_ROCK, P_BUSH, P_CACTUS, P_PALM, P_KIND_COUNT };

// Variants: the same clay miniature in a few hand-made places, one per song. Each is a coherent set: seven
// piece colors (I O T S Z J L, every role keeps its hue family so the pieces read apart), the landscape
// materials (palette slots above), the water, the sky of dawn / day / golden hour, and its trees.
// More can be added to the table; the song picks one from its key, its mood and the scene seed.
struct Variant {
    const char* name;
    uint32_t piece[7];
    uint32_t c[16];
    uint32_t shallow, deep;
    uint32_t sky[3][2]; // zenith, horizon at dawn, day, golden hour (dusk and night are shared)
    int big, small, accent; // tree shapes
    int night = 0; // 1: a night place (moonlight; its skies are calm / mid / peak nights, not dawn / day / golden hour)
};
const Variant VARIANTS[] = {
    {"MEADOW COVE",
     {0x5EC2C6, 0xF3CC57, 0xA68AD9, 0x86C377, 0xEC7864, 0x5B8BD9, 0xF39E4C},
     {0xA9C98A, 0x93BC78, 0xEFDDB4, 0xCFA987, 0xBDB6A8, 0x8E6E55, 0x86B86E, 0x69A064, 0xF3E8D4, 0xE2CBAF, 0xEADFCB,
      0xE9DDCA, 0x97BF8E, 0x8AAEAA, 0xA9BCCD, 0xFFC77A},
     0x86D6C8, 0x3F8DB0, {{0x5468A0, 0xF0BFB2}, {0x5E9BDB, 0xCFE3F0}, {0x6575B0, 0xF7BC86}}, P_ROUND, P_ROUND, P_CYPRESS},
    {"TERRACOTTA MESA",
     {0x4FB8B0, 0xF0C75E, 0xB07FC9, 0x9DBB6A, 0xD9604A, 0x4F7FC4, 0xEE9447},
     {0xE7C391, 0xDDB07E, 0xF2DAAE, 0xD08B66, 0xC07E64, 0x7D5C46, 0x8FA86A, 0x6C8A55, 0xF5DFC8, 0xE8BD9A, 0xF0D4B9,
      0xEDD6C0, 0xDC9D78, 0xD0968A, 0xDEB4AA, 0xFFC27A},
     0x5FD6CC, 0x23889F, {{0x6A64A0, 0xF3B9A0}, {0x6FA3D6, 0xF1DCC4}, {0x7A6AA8, 0xFFB070}}, P_CYPRESS, P_CACTUS, P_ROUND},
    {"LAVENDER SNOW",
     {0x5FC3D0, 0xF2CF5C, 0xA88AE0, 0x7FC88A, 0xEC7474, 0x6688E0, 0xF29E5C},
     {0xF1F3F7, 0xE1E6F0, 0xDCDDE9, 0xAFA9CA, 0xA29EBE, 0x6F5B63, 0x5F8E86, 0x4A7778, 0xF6F2F4, 0xDDD4E8, 0xECE6F0,
      0xE6E1EC, 0xCFCEE6, 0xB7B5DB, 0xCFD0EA, 0xFFC98A},
     0x93C6D9, 0x4E78A8, {{0x6474B0, 0xE9C6D6}, {0x7FA6DA, 0xE4ECF6}, {0x7680BE, 0xF5C3A6}}, P_PINE, P_PINE, P_PINE},
    {"PINE AND SEA",
     {0x4FB3C9, 0xF7D25C, 0x9C7BD0, 0x6FBF8A, 0xF2705E, 0x3F76C8, 0xF79A56},
     {0x8FB37A, 0x7DA56B, 0xE6DCC4, 0xA89A88, 0x9C9A94, 0x6E5544, 0x3F6E5A, 0x2F5C4C, 0xF2ECE2, 0xDCD2C4, 0xE8E1D6,
      0xE4DCCE, 0x6F9A86, 0x6E8EA0, 0x9AAFC2, 0xFFC77A},
     0x5FB9C9, 0x24648F, {{0x4A6496, 0xD8C2C4}, {0x4F8ED0, 0xC8DDE8}, {0x5A6EA8, 0xF2B888}}, P_PINE, P_PINE, P_BUSH},
    {"AUTUMN MAPLES",
     {0x5AA9A6, 0xE8B23C, 0x9E6FB0, 0x8FAE4E, 0xCF4E3A, 0x4E73B5, 0xE5822F},
     {0xD8B26A, 0xC99E58, 0xEAD7B0, 0xB98A66, 0xA99C8E, 0x6E4E3C, 0xE0743A, 0xC9473A, 0xF4E6D0, 0xE3C9A6, 0xEDDFC8,
      0xE6D6BE, 0xC99A62, 0xB48A78, 0xB8A7A8, 0xFFC27A},
     0x6FB8C0, 0x33789C, {{0x5E6098, 0xF0C2A0}, {0x6E9CCF, 0xEBDCC2}, {0x6A62A0, 0xFFA866}}, P_ROUND, P_ROUND, P_CYPRESS},
    {"MANGO LAGOON",
     {0x2EC4C0, 0xFFD24A, 0xB27AE0, 0x6FD27A, 0xFF6B6B, 0x4C8DF0, 0xFF9F43},
     {0x8CCB6A, 0x72B858, 0xF7EBC8, 0xD9B98C, 0xC7B8A4, 0x8A6A4A, 0x4FB060, 0x2F9450, 0xF6EEE0, 0xEAD9BC, 0xF1E6D2,
      0xEDE2CF, 0x6FB98A, 0x70AFB4, 0xA2C6D6, 0xFFC77A},
     0x4FE0D0, 0x1F9FC0, {{0x5B70B8, 0xFFC1B0}, {0x3FA2E8, 0xBFEAF2}, {0x5A6CB8, 0xFFB27A}}, P_PALM, P_PALM, P_BUSH},
    {"SAKURA STONES",
     {0x4FB8BE, 0xEFC85E, 0xA07ED0, 0x82BF74, 0xE8738C, 0x5C80CE, 0xEE9A5E},
     {0x9DB88A, 0x86A676, 0xE6DBC8, 0xA8A49A, 0x9A988F, 0x6A5450, 0xF4B6C6, 0xE596AE, 0xF3EDE6, 0xDDD3CC, 0xEBE4DE,
      0xE6DED7, 0xA8BC98, 0xB5A9BE, 0xC9BDD2, 0xFFC6A0},
     0x8CCFC8, 0x4A8AA6, {{0x6A6EAA, 0xF6CAD2}, {0x86AEE0, 0xF2E2EA}, {0x7A74B4, 0xFFC0A0}}, P_ROUND, P_ROUND, P_BUSH},
    {"JADE VOLCANO",
     {0x3FC0A8, 0xF2C94C, 0xA486E0, 0x9ACD5B, 0xF06A4F, 0x5A86E0, 0xF59A3E},
     {0x5E5C62, 0x4E4D54, 0x3E3D44, 0x6A5E5C, 0x4E4A4C, 0x4A3A34, 0x5FA87A, 0x3F8A62, 0xEDE6DC, 0xD6CCC0, 0xE2DACE,
      0xE0D8CC, 0x5C6070, 0x707488, 0x9AA0B4, 0xFFB070},
     0x4FC9A8, 0x1E6E70, {{0x4E5A8A, 0xE0B0A0}, {0x6A90B8, 0xD6DCE0}, {0x5A5A90, 0xF09A6A}}, P_CYPRESS, P_BUSH, P_PINE},
    {"HEATHER HIGHLANDS",
     {0x6CBFC2, 0xEEC965, 0x9C7CCB, 0x8DBF7A, 0xE07870, 0x6585D0, 0xECA05A},
     {0xA58FBC, 0x8A9C78, 0xDCD4C4, 0x9E9488, 0x948E8A, 0x5E4E48, 0x6E8A62, 0x586F52, 0xEFEAE6, 0xD8CED0, 0xE6E0DE,
      0xE2DBD8, 0x9C8EB4, 0x8F96B0, 0xB3B8CC, 0xFFC27A},
     0x7FA9C0, 0x3E6488, {{0x5A6090, 0xD9C0CC}, {0x7894BE, 0xD8DEE6}, {0x6A6498, 0xEEB290}}, P_PINE, P_BUSH, P_BUSH},
    // Night places: the same island under the moon, the tower's lanterns lit. Their pieces are brighter and a
    // touch more saturated so the stack reads against the dark sky.
    {"MOONLIT COVE",
     {0x6FE3EA, 0xFFE07A, 0xC6A4FF, 0x9AEB98, 0xFF8A8E, 0x86A8FF, 0xFFB46A},
     {0x9EC4A0, 0x88B490, 0xE8E0CC, 0xB8AAA0, 0xB0B0B4, 0x7E6656, 0x6FA88A, 0x558E78, 0xB4AEB0, 0x9C969C, 0xA8A2A6,
      0x9A949A, 0x7FA0A8, 0x7090AC, 0x8EA2C8, 0xFFC27A},
     0x5FA8C0, 0x1C3E6E, {{0x0C1638, 0x2A3E6A}, {0x0E1C46, 0x34507E}, {0x1A1A4A, 0x5A4878}}, P_ROUND, P_ROUND, P_CYPRESS, 1},
    {"LANTERN HARBOR",
     {0x5FDCD2, 0xFFD45E, 0xD69AF0, 0xA8E07A, 0xFF7466, 0x7C9CFF, 0xFFA04E},
     {0xC8A882, 0xB89670, 0xEAD6B4, 0xC48A6A, 0xB08470, 0x7A5640, 0x7E9A6A, 0x62805A, 0xB89A80, 0xA08470, 0xAC9078,
      0x9A7E6A, 0xA87E70, 0x8A7488, 0x9A8CAE, 0xFFB45E},
     0x4F9AB0, 0x182E5C, {{0x180E30, 0x4A2A50}, {0x1C1238, 0x5A3458}, {0x24143A, 0x844A5E}}, P_CYPRESS, P_PALM, P_ROUND, 1},
    {"FIREFLY FJORD",
     {0x62D8E8, 0xF8E46A, 0xB4A0FF, 0x86F0A8, 0xFF7C9C, 0x6C9CFF, 0xFFA670},
     {0x7FA48A, 0x6C9480, 0xD8D6CC, 0x9C9AA2, 0x929298, 0x5E5050, 0x3F7468, 0x2F6058, 0xA4AAB0, 0x8C9298, 0x9AA0A6,
      0x8E949A, 0x5F8A8A, 0x5C7A98, 0x7A8EB8, 0xFFCC80},
     0x4AA0B4, 0x10305A, {{0x061A24, 0x184A4C}, {0x081E2C, 0x1E5A58}, {0x0A2032, 0x2E6A62}}, P_PINE, P_PINE, P_BUSH, 1},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));
const char* TOD_NAMES[4] = {"DAWN", "DAY", "GOLDEN HOUR", "DUSK"};

// The song's variant: its key (the hue follows the circle of fifths), its mode and the seed.
int variantOf(const SongLook& l) {
    float h = std::fmod(l.hue, TAU);
    if (h < 0) h += TAU;
    const int hueBin = std::min(VARIANT_COUNT - 1, (int)(h / TAU * VARIANT_COUNT));
    return (hueBin + 3 * (l.minor ? 1 : 0) + (int)((l.seed >> 17) % 2)) % VARIANT_COUNT;
}
float todOf(int level, float energy) {
    const float e = saturate(energy);
    if (level <= 0) return 0.05f + 0.25f * e;
    if (level == 1) return 0.9f + 0.25f * e;
    return std::min(2.6f, 1.95f + 0.5f * e * e);
}

// The light at five moments. Directions point toward the light; "sky sun" is where the sun is painted in
// the backdrop (the light itself comes from the front side so the board's faces stay readable).
struct TodKey {
    uint32_t zenith, horizon, sun; float sunI; uint32_t sky; float skyI; uint32_t ground; float groundI;
    uint32_t shadowTint, sunFog;
    float fog, mist, glow, rim, cloud, wind, wave, lantern, exposure, stars;
    vec3 sunDir, skySun;
};
const TodKey TOD[4] = {
    // dawn / blue hour: low pink sun on the left, mist on still water
    {0x5468A0, 0xF0BFB2, 0xFFC9AA, 2.1f, 0x9DAED8, 0.7f, 0xD9B3AA, 0.32f, 0xBBA8DA, 0xFFB79E,
     0.0045f, 0.5f, 0.85f, 0.25f, 0.3f, 0.12f, 0.12f, 0.f, 0.95f, 0.12f, {-0.78f, 0.26f, 0.58f}, {-0.62f, 0.045f, -0.78f}},
    // day: high soft sun, drifting clouds
    {0x5E9BDB, 0xCFE3F0, 0xFFF2DE, 2.9f, 0xA9C4E8, 0.62f, 0xDCCBA8, 0.32f, 0xAAB8E2, 0xFFF0D8,
     0.0028f, 0.08f, 0.4f, 0.12f, 0.62f, 0.35f, 0.45f, 0.f, 0.88f, 0.f, {-0.38f, 0.82f, 0.45f}, {-0.25f, 0.6f, -0.76f}},
    // golden hour: low warm sun on the right, long shadows, rim light
    {0x6575B0, 0xF7BC86, 0xFFAD6C, 3.0f, 0xAE9FCC, 0.55f, 0xE2AA86, 0.36f, 0xBB9CCB, 0xFFA060,
     0.003f, 0.12f, 1.f, 0.6f, 0.45f, 0.55f, 0.6f, 0.12f, 0.9f, 0.f, {0.62f, 0.3f, 0.72f}, {0.55f, 0.075f, -0.83f}},
    // dusk: the sun on the horizon, lanterns lit, fireflies
    {0x3C4586, 0xF0957A, 0xFF8E62, 1.9f, 0x8A80B8, 0.52f, 0xC99488, 0.3f, 0x9E8ACB, 0xFF7E55,
     0.0028f, 0.12f, 1.f, 0.7f, 0.4f, 0.6f, 0.6f, 1.f, 0.98f, 0.15f, {0.7f, 0.2f, 0.68f}, {0.6f, 0.012f, -0.8f}},
};
// night: the light of the night places, and the slow-motion bonus elsewhere (moonlight, the blocks become lanterns)
const TodKey NIGHT = {0x0A1030, 0x1E2A52, 0x9DB4FF, 0.62f, 0x41528E, 0.36f, 0x262840, 0.2f, 0x7078B8, 0x7F98D8,
                      0.005f, 0.3f, 0.5f, 0.4f, 0.15f, 0.2f, 0.18f, 1.f, 1.0f, 1.f, {-0.45f, 0.62f, 0.62f}, {-0.35f, 0.3f, -0.88f}};
// still night: the slow-motion bonus in a night place (time stands still: a pale silver moon, a green glow on the
// horizon, every star out, the water glassy, the blocks become lanterns)
const TodKey STILL = {0x080E30, 0x1F6470, 0xCFE4FF, 0.66f, 0x5470B0, 0.4f, 0x2E3450, 0.22f, 0x8A9ADA, 0x8FF0D8,
                      0.004f, 0.22f, 0.9f, 0.55f, 0.f, 0.03f, 0.04f, 0.35f, 0.88f, 1.f, {0.1f, 0.7f, 0.7f}, {0.05f, 0.42f, -0.9f}};
const float TOD_AT[4] = {0.f, 1.f, 2.f, 2.6f};
const char* NIGHT_NAMES[4] = {"MOONRISE", "MIDNIGHT", "STARLIGHT", "LANTERN HOUR"};

// A light (one key, or a blend of keys) in linear values.
struct Light {
    vec3 zenith, horizon, sun, sky, ground, shadowTint, sunFog, sunDir, skySun;
    float sunI, skyI, groundI, fog, mist, glow, rim, cloud, wind, wave, lantern, exposure, stars;
};
Light lightOf(const TodKey& k) {
    return {hex(k.zenith), hex(k.horizon), hex(k.sun), hex(k.sky), hex(k.ground), hex(k.shadowTint), hex(k.sunFog), k.sunDir,
            k.skySun, k.sunI, k.skyI, k.groundI, k.fog, k.mist, k.glow, k.rim, k.cloud, k.wind, k.wave, k.lantern, k.exposure,
            k.stars};
}
Light mixLight(const Light& a, const Light& b, float t) {
    Light r;
    r.zenith = lerp(a.zenith, b.zenith, t);
    r.horizon = lerp(a.horizon, b.horizon, t);
    r.sun = lerp(a.sun, b.sun, t);
    r.sky = lerp(a.sky, b.sky, t);
    r.ground = lerp(a.ground, b.ground, t);
    r.shadowTint = lerp(a.shadowTint, b.shadowTint, t);
    r.sunFog = lerp(a.sunFog, b.sunFog, t);
    r.sunDir = lerp(a.sunDir, b.sunDir, t);
    r.skySun = lerp(a.skySun, b.skySun, t);
    r.sunI = lerpf(a.sunI, b.sunI, t);
    r.skyI = lerpf(a.skyI, b.skyI, t);
    r.groundI = lerpf(a.groundI, b.groundI, t);
    r.fog = lerpf(a.fog, b.fog, t);
    r.mist = lerpf(a.mist, b.mist, t);
    r.glow = lerpf(a.glow, b.glow, t);
    r.rim = lerpf(a.rim, b.rim, t);
    r.cloud = lerpf(a.cloud, b.cloud, t);
    r.wind = lerpf(a.wind, b.wind, t);
    r.wave = lerpf(a.wave, b.wave, t);
    r.lantern = lerpf(a.lantern, b.lantern, t);
    r.exposure = lerpf(a.exposure, b.exposure, t);
    r.stars = lerpf(a.stars, b.stars, t);
    return r;
}

// Island and world layout.
constexpr float WATER_Y = -12.8f, GROUND_Y = -11.6f;
constexpr float ISLAND_X = 0.f, ISLAND_Z = -1.2f, ISLAND_RX = 15.f, ISLAND_RZ = 7.6f;
using layout::BOARD_W;
using layout::BOARD_H;

// ---------------------------------------------------------------------------------------------------------
// Mesh building. World vertices: position, normal, (palette slot, shade, sway weight).
struct WV { vec3 p, n; float slot, shade, sway; };

void pushWV(std::vector<float>& out, const WV& v) {
    const float f[9] = {v.p.x, v.p.y, v.p.z, v.n.x, v.n.y, v.n.z, v.slot, v.shade, v.sway};
    out.insert(out.end(), f, f + 9);
}
// A flat-shaded triangle (low-poly look); winding fixed to face `outward` when given.
void flatTri(std::vector<float>& out, vec3 a, vec3 b, vec3 c, float slot, float shade, float swayA, float swayB, float swayC,
             vec3 outward = vec3(0.f)) {
    vec3 n = normalize(cross(b - a, c - a));
    if (dot(outward, outward) > 0 && dot(n, outward) < 0) { std::swap(b, c); std::swap(swayB, swayC); n = n * -1.f; }
    pushWV(out, {a, n, slot, shade, swayA});
    pushWV(out, {b, n, slot, shade, swayB});
    pushWV(out, {c, n, slot, shade, swayC});
}

// Rounded box with a true bevel radius (smooth normals), built from a non-uniform grid on each face.
void roundedBox(std::vector<float>& out, vec3 c, vec3 size, float r, int slot, float shade = 1.f) {
    const vec3 h = size * 0.5f;
    r = std::min(r, std::min(h.x, std::min(h.y, h.z)) * 0.95f);
    const int B = 4;
    auto samples = [&](float hh) {
        std::vector<float> s;
        for (int k = 0; k <= B; k++) s.push_back(-hh + r * (1.f - std::cos(k / (float)B * PI * 0.5f)));
        for (int k = B; k >= 0; k--) s.push_back(hh - r * (1.f - std::cos(k / (float)B * PI * 0.5f)));
        return s;
    };
    auto place = [&](vec3 p, vec3& pos, vec3& nrm) {
        vec3 in(clampf(p.x, -h.x + r, h.x - r), clampf(p.y, -h.y + r, h.y - r), clampf(p.z, -h.z + r, h.z - r));
        vec3 d = p - in;
        nrm = normalize(d);
        pos = c + in + nrm * r;
    };
    for (int axis = 0; axis < 3; axis++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
            const float hv[3] = {h.x, h.y, h.z};
            std::vector<float> su = samples(hv[ua]), sv = samples(hv[va]);
            for (size_t i = 0; i + 1 < su.size(); i++)
                for (size_t j = 0; j + 1 < sv.size(); j++) {
                    if (su[i + 1] - su[i] < 1e-5f || sv[j + 1] - sv[j] < 1e-5f) continue;
                    vec3 P[4], N[4];
                    const float uu[4] = {su[i], su[i + 1], su[i + 1], su[i]}, vv[4] = {sv[j], sv[j], sv[j + 1], sv[j + 1]};
                    for (int k = 0; k < 4; k++) {
                        float q[3];
                        q[axis] = sgn * hv[axis];
                        q[ua] = uu[k];
                        q[va] = vv[k];
                        place(vec3(q[0], q[1], q[2]), P[k], N[k]);
                    }
                    vec3 fn(0.f);
                    (&fn.x)[axis] = (float)sgn;
                    const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
                    for (auto& t : tri) {
                        int a = t[0], b = t[1], cc = t[2];
                        if (dot(cross(P[b] - P[a], P[cc] - P[a]), fn) < 0) std::swap(b, cc);
                        pushWV(out, {P[a], N[a], (float)slot, shade, 0});
                        pushWV(out, {P[b], N[b], (float)slot, shade, 0});
                        pushWV(out, {P[cc], N[cc], (float)slot, shade, 0});
                    }
                }
        }
}

// Cylinder along an axis (0 x, 1 y, 2 z), smooth sides, flat caps.
void cylinder(std::vector<float>& out, vec3 c, int axis, float radius, float length, int seg, int slot, float shade = 1.f,
              float swayBase = 0, float swayTop = 0) {
    auto P = [&](float a, float t) {
        float q[3];
        q[axis] = t;
        q[(axis + 1) % 3] = std::cos(a) * radius;
        q[(axis + 2) % 3] = std::sin(a) * radius;
        return c + vec3(q[0], q[1], q[2]);
    };
    auto Nr = [&](float a) {
        float q[3];
        q[axis] = 0;
        q[(axis + 1) % 3] = std::cos(a);
        q[(axis + 2) % 3] = std::sin(a);
        return vec3(q[0], q[1], q[2]);
    };
    vec3 ax(0.f);
    (&ax.x)[axis] = 1.f;
    const float h = length * 0.5f;
    for (int i = 0; i < seg; i++) {
        float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
        vec3 p00 = P(a0, -h), p10 = P(a1, -h), p01 = P(a0, h), p11 = P(a1, h);
        vec3 n0 = Nr(a0), n1 = Nr(a1);
        auto put = [&](vec3 a, vec3 na, float sa, vec3 b, vec3 nb, float sb, vec3 cc, vec3 nc, float sc) {
            if (dot(cross(b - a, cc - a), na + nb + nc) < 0) { std::swap(b, cc); std::swap(nb, nc); std::swap(sb, sc); }
            pushWV(out, {a, na, (float)slot, shade, sa});
            pushWV(out, {b, nb, (float)slot, shade, sb});
            pushWV(out, {cc, nc, (float)slot, shade, sc});
        };
        put(p00, n0, swayBase, p10, n1, swayBase, p11, n1, swayTop);
        put(p00, n0, swayBase, p11, n1, swayTop, p01, n0, swayTop);
        put(c - ax * h, ax * -1.f, swayBase, p10, ax * -1.f, swayBase, p00, ax * -1.f, swayBase);
        put(c + ax * h, ax, swayTop, p01, ax, swayTop, p11, ax, swayTop);
    }
}

// Icosphere vertices (unit), subdivided.
void icosphere(int sub, std::vector<vec3>& verts, std::vector<int>& idx) {
    const float t = (1.f + std::sqrt(5.f)) / 2.f;
    verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
             {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& v : verts) v = normalize(v);
    idx = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
           3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < sub; s++) {
        std::vector<int> ni;
        auto mid = [&](int a, int b) {
            verts.push_back(normalize((verts[a] + verts[b]) * 0.5f));
            return (int)verts.size() - 1;
        };
        for (size_t i = 0; i < idx.size(); i += 3) {
            int a = idx[i], b = idx[i + 1], c = idx[i + 2];
            int ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            int tri[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            ni.insert(ni.end(), tri, tri + 12);
        }
        idx = ni;
    }
}

// A faceted blob (canopy, rock, bush): an icosphere scaled, jittered (by position, so shared corners stay
// shared), flat shaded.
void blob(std::vector<float>& out, vec3 c, vec3 scale, int sub, int slot, int slot2, float jitter, uint64_t seed, float swayBase,
          float swayTop, float baseY, float topY) {
    std::vector<vec3> v;
    std::vector<int> idx;
    icosphere(sub, v, idx);
    std::vector<vec3> P(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(v[i].x * 1000) * 73856093ull ^
                                (uint64_t)(int64_t)std::lround(v[i].y * 1000) * 19349663ull ^
                                (uint64_t)(int64_t)std::lround(v[i].z * 1000) * 83492791ull ^ seed);
        float j = 1.f + jitter * (((h >> 40) & 0xFFFF) / 65535.f - 0.5f) * 2.f;
        P[i] = c + v[i] * scale * j;
    }
    for (size_t i = 0; i < idx.size(); i += 3) {
        vec3 a = P[idx[i]], b = P[idx[i + 1]], cc = P[idx[i + 2]];
        vec3 ctr = (a + b + cc) / 3.f;
        vec3 n = normalize(cross(b - a, cc - a));
        const int sl = (n.y > 0.3f || ((i / 3) % 3 == 0)) ? slot : slot2;
        auto sw = [&](vec3 p) { return lerpf(swayBase, swayTop, saturate((p.y - baseY) / std::max(0.01f, topY - baseY))); };
        flatTri(out, a, b, cc, (float)sl, 1.f, sw(a), sw(b), sw(cc), ctr - c);
    }
}

void cone(std::vector<float>& out, vec3 base, float radius, float height, int seg, int slot, int slot2, float swayBase,
          float swayTop) {
    vec3 tip = base + vec3(0, height, 0);
    for (int i = 0; i < seg; i++) {
        float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
        vec3 p0 = base + vec3(std::cos(a0) * radius, 0, std::sin(a0) * radius);
        vec3 p1 = base + vec3(std::cos(a1) * radius, 0, std::sin(a1) * radius);
        vec3 mid = (p0 + p1) * 0.5f - base;
        flatTri(out, p0, p1, tip, (float)(i % 2 ? slot : slot2), 1.f, swayBase, swayBase, swayTop, mid + vec3(0, radius * 0.5f, 0));
        flatTri(out, p0, base, p1, (float)slot2, 0.8f, swayBase, swayBase, swayBase, vec3(0, -1, 0));
    }
}

// Smooth sphere (lanterns, beads).
void sphere(std::vector<float>& out, vec3 c, float radius, int slot) {
    std::vector<vec3> v;
    std::vector<int> idx;
    icosphere(2, v, idx);
    for (size_t i = 0; i < idx.size(); i += 3) {
        vec3 a = v[idx[i]], b = v[idx[i + 1]], cc = v[idx[i + 2]];
        if (dot(cross(b - a, cc - a), a + b + cc) < 0) std::swap(b, cc);
        pushWV(out, {c + a * radius, a, (float)slot, 1.f, 0});
        pushWV(out, {c + b * radius, b, (float)slot, 1.f, 0});
        pushWV(out, {c + cc * radius, cc, (float)slot, 1.f, 0});
    }
}

float noise1(float x, uint64_t seed) {
    int i = (int)std::floor(x);
    float f = x - i;
    auto h = [&](int k) { return ((splitmix64((uint64_t)(k + 100000) ^ seed) >> 40) & 0xFFFF) / 65535.f; };
    float u = f * f * (3 - 2 * f);
    return lerpf(h(i), h(i + 1), u);
}

} // namespace

// =========================================================================================================

GLuint DioramaUniverse::compile(const char* vs, const char* fs, const char* name) {
    GLuint p = glCreateProgram();
    GLuint a = compileStage(GL_VERTEX_SHADER, vs, name), b = compileStage(GL_FRAGMENT_SHADER, fs, name);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s link error:\n%s\n", name, log);
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

bool DioramaUniverse::init(int width, int height) {
    using namespace shaders;
    const std::string V = "#version 330 core\n", D = "#version 330 core\n#define DEPTH_ONLY\n";
    progBlock_ = compile((V + BLOCK_VS).c_str(), (V + COMMON + BLOCK_FS).c_str(), "block");
    progBlockDepth_ = compile((V + BLOCK_VS).c_str(), (D + COMMON + BLOCK_FS).c_str(), "block-depth");
    progWorld_ = compile((V + WORLD_VS).c_str(), (V + COMMON + WORLD_FS).c_str(), "world");
    progWorldDepth_ = compile((V + WORLD_VS).c_str(), (D + COMMON + WORLD_FS).c_str(), "world-depth");
    progSky_ = compile((V + SKY_VS).c_str(), (V + COMMON + SKY_FS).c_str(), "sky");
    progWater_ = compile((V + WATER_VS).c_str(), (V + COMMON + WATER_FS).c_str(), "water");
    progSprite_ = compile((V + SPRITE_VS).c_str(), (V + COMMON + SPRITE_FS).c_str(), "sprite");
    progDown_ = compile(POST_VS, DOWN_FS, "down");
    progBlur_ = compile(POST_VS, BLUR_FS, "blur");
    progComp_ = compile(POST_VS, COMP_FS, "composite");
    progPanel_ = compile(HUD_VS, PANEL_FS, "panel");
    progText_ = compile(HUD_VS, TEXT_FS, "text");

    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &instVbo_);

    // Instanced meshes: the clay block (rounded cube, unit size) and a sphere (gauge beads).
    {
        std::vector<float> w, v;
        roundedBox(w, vec3(0.f), vec3(1.f), 0.13f, 0);
        for (size_t i = 0; i < w.size(); i += 9) v.insert(v.end(), w.begin() + i, w.begin() + i + 6);
        uploadInstMesh(cube_, v);
        w.clear();
        v.clear();
        sphere(w, vec3(0.f), 0.5f, 0);
        for (size_t i = 0; i < w.size(); i += 9) v.insert(v.end(), w.begin() + i, w.begin() + i + 6);
        uploadInstMesh(sphere_, v);
    }
    // Sprites.
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenBuffers(1, &spriteVbo_);
    glGenVertexArrays(1, &spriteVao_);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    for (int i = 0; i < 3; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(1 + i, 1);
    }
    // HUD.
    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    // Board occupancy (contact shading on the back wall).
    glGenTextures(1, &occTex_);
    glBindTexture(GL_TEXTURE_2D, occTex_);
    std::vector<unsigned char> zero(12 * 22, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 12, 22, 0, GL_RED, GL_UNSIGNED_BYTE, zero.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Shadow map.
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] shadow FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    buildWorld();
    buildFont();

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(4, (int)maxSamples);
    resize(width, height);
    computeEnv(0.f, MusicState{});
    return glGetError() == GL_NO_ERROR;
}

void DioramaUniverse::uploadWorld(Mesh& m, const std::vector<float>& v) {
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    m.count = (int)(v.size() / 9);
}

void DioramaUniverse::uploadInstMesh(Mesh& m, const std::vector<float>& v) {
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void*)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(2 + i, 1);
    }
    glBindVertexArray(0);
    m.count = (int)(v.size() / 6);
}

// The static world: the island, the clay tower holding the well, two steles for the previews, the gauge's
// dial, distant layered hills; trees and rocks are separate props (they regrow when the landscape changes).
void DioramaUniverse::buildWorld() {
    // ---- Island: an elliptical low-poly mound: grass top, a soft bank, a sand beach sloping under the water.
    {
        std::vector<float> v;
        const int RINGS = 26, SEG = 84;
        auto rho = [&](int i) { return std::pow(i / (float)RINGS, 0.75f) * 1.32f; };
        auto height = [&](float r, float a) {
            float n = 0.1f * (noise1(a * 6.f, 11) - 0.5f) + 0.06f * (noise1(a * 17.f + r * 5.f, 12) - 0.5f);
            if (r < 0.8f) return GROUND_Y + n * smoothstepf(0.3f, 0.8f, r) * 2.f;
            if (r < 0.95f) return lerpf(GROUND_Y + n * 2.f, WATER_Y + 0.35f, smoothstepf(0.8f, 0.95f, r));
            if (r < 1.06f) return lerpf(WATER_Y + 0.35f, WATER_Y - 0.35f, (r - 0.95f) / 0.11f);
            return lerpf(WATER_Y - 0.35f, WATER_Y - 3.5f, saturate((r - 1.06f) / 0.26f));
        };
        auto P = [&](int i, int j) {
            float r = rho(i), a = TAU * j / SEG;
            float wob = 1.f + 0.05f * (noise1(a * 5.f, 7) - 0.5f);
            return vec3(ISLAND_X + std::cos(a) * r * wob * ISLAND_RX, height(r, a), ISLAND_Z + std::sin(a) * r * wob * ISLAND_RZ);
        };
        for (int i = 0; i < RINGS; i++)
            for (int j = 0; j < SEG; j++) {
                vec3 a = P(i, j), b = P(i, j + 1), c = P(i + 1, j + 1), d = P(i + 1, j);
                auto slotOf = [&](vec3 p0, vec3 p1, vec3 p2) {
                    float y = (p0.y + p1.y + p2.y) / 3.f;
                    if (y > GROUND_Y - 0.25f) return ((splitmix64(i * 131 + j) >> 50) % 5 == 0) ? (float)S_GRASS2 : (float)S_GRASS;
                    if (y > WATER_Y + 0.5f) return (float)S_CLIFF;
                    return (float)S_SAND;
                };
                if (i == 0) {
                    flatTri(v, a, c, d, slotOf(a, c, d), 1.f, 0, 0, 0, vec3(0, 1, 0));
                } else {
                    flatTri(v, a, b, c, slotOf(a, b, c), 1.f, 0, 0, 0, vec3(0, 1, 0));
                    flatTri(v, a, c, d, slotOf(a, c, d), 1.f, 0, 0, 0, vec3(0, 1, 0));
                }
            }
        // Distant hills: three layered ridges, fading into the air.
        struct Layer { float z, depth, amp, x; int slot; uint64_t seed; float freq; };
        const Layer layers[3] = {{-48, 26, 9, 230, S_HILL1, 31, 0.05f}, {-105, 45, 22, 330, S_HILL2, 32, 0.03f},
                                 {-210, 80, 46, 520, S_HILL3, 33, 0.018f}};
        for (const Layer& L : layers) {
            const int NX = 70, NZ = 7;
            auto H = [&](int ix, int iz) {
                float x = -L.x + 2 * L.x * ix / NX, zf = iz / (float)NZ;
                float ridge = 0.25f + 0.75f * noise1(x * L.freq, L.seed) + 0.25f * noise1(x * L.freq * 3.1f, L.seed + 5);
                // Lower behind the board (the frame stays calm there), higher toward the sides.
                ridge *= 0.55f + 0.45f * smoothstepf(10.f, 70.f, std::fabs(x));
                float bump = std::pow(std::sin(PI * std::min(1.f, zf * 1.3f)), 0.8f);
                return vec3(x, WATER_Y - 0.6f + L.amp * ridge * bump, L.z - L.depth * zf);
            };
            for (int ix = 0; ix < NX; ix++)
                for (int iz = 0; iz < NZ; iz++) {
                    vec3 a = H(ix, iz), b = H(ix + 1, iz), c = H(ix + 1, iz + 1), d = H(ix, iz + 1);
                    flatTri(v, a, b, c, (float)L.slot, 1.f, 0, 0, 0, vec3(0, 1, 0.3f));
                    flatTri(v, a, c, d, (float)L.slot, 1.f, 0, 0, 0, vec3(0, 1, 0.3f));
                }
        }
        // Two islets in the middle distance.
        auto islet = [&](vec3 c, float rx, float rz, float hgt) {
            const int R = 5, S = 22;
            auto Q = [&](int i, int j) {
                float r = i / (float)R * 1.25f, a = TAU * j / S;
                float y = r < 0.75f ? WATER_Y + hgt * (1.f - r * r * 0.6f) : lerpf(WATER_Y + hgt * 0.66f, WATER_Y - 1.5f, (r - 0.75f) / 0.5f);
                return vec3(c.x + std::cos(a) * r * rx, y + 0.3f * (noise1(a * 3 + c.x, 5) - 0.5f), c.z + std::sin(a) * r * rz);
            };
            for (int i = 0; i < R; i++)
                for (int j = 0; j < S; j++) {
                    vec3 a = Q(i, j), b = Q(i, j + 1), cc = Q(i + 1, j + 1), d = Q(i + 1, j);
                    float sl = a.y > WATER_Y + hgt * 0.6f ? S_GRASS : (a.y > WATER_Y + 0.3f ? S_CLIFF : S_SAND);
                    if (i > 0) flatTri(v, a, b, cc, sl, 1.f, 0, 0, 0, vec3(0, 1, 0));
                    flatTri(v, a, cc, d, sl, 1.f, 0, 0, 0, vec3(0, 1, 0));
                }
        };
        islet(vec3(-36, 0, -32), 6.f, 3.5f, 1.6f);
        islet(vec3(44, 0, -40), 8.f, 4.f, 2.4f);
        uploadWorld(island_, v);
    }

    // ---- The tower: back wall, two pillars with caps and lanterns, the well's floor, a stepped plinth;
    //      two steles with arched tops for the previews; the gauge's dial.
    {
        std::vector<float> v;
        roundedBox(v, vec3(0, 0, -0.78f), vec3(BOARD_W + 0.1f, BOARD_H + 0.1f, 0.56f), 0.05f, S_WALL);
        for (int s = -1; s <= 1; s += 2) {
            roundedBox(v, vec3(s * 5.42f, 0.15f, -0.32f), vec3(0.84f, 20.9f, 1.62f), 0.16f, S_TOWER);
            roundedBox(v, vec3(s * 5.42f, 10.72f, -0.22f), vec3(1.2f, 0.36f, 2.05f), 0.12f, S_TOWER2);
            roundedBox(v, vec3(s * 5.42f, 11.0f, -0.22f), vec3(0.62f, 0.22f, 0.62f), 0.08f, S_TOWER);
            sphere(v, vec3(s * 5.42f, 11.42f, -0.22f), 0.34f, S_LANTERN);
            // Steles.
            roundedBox(v, vec3(s * 8.6f, -1.75f, -0.98f), vec3(3.9f, 19.7f, 0.72f), 0.14f, S_STELE);
            cylinder(v, vec3(s * 8.6f, 8.1f, -0.98f), 2, 1.95f, 0.72f, 48, S_STELE);
            roundedBox(v, vec3(s * 8.6f, -11.25f, -0.95f), vec3(4.5f, 0.8f, 1.35f), 0.12f, S_TOWER2);
        }
        roundedBox(v, vec3(0, -10.32f, -0.22f), vec3(10.1f, 0.64f, 1.75f), 0.1f, S_TOWER2);
        roundedBox(v, vec3(0, -10.95f, -0.25f), vec3(12.6f, 0.9f, 2.5f), 0.16f, S_TOWER);
        roundedBox(v, vec3(0, -11.55f, -0.25f), vec3(13.8f, 0.6f, 3.3f), 0.14f, S_TOWER2);
        // The gauge's dial: a clay disc behind the beads.
        const vec3 g = layout::GAUGE_CENTER;
        cylinder(v, vec3(g.x, g.y, -0.5f), 2, layout::GAUGE_R + 0.42f, 0.28f, 48, S_TOWER2);
        uploadWorld(tower_, v);
    }

    // ---- Props: tree and rock meshes, placed per landscape.
    {
        auto addMesh = [&](const std::vector<float>& v) {
            Mesh m;
            uploadWorld(m, v);
            propMeshes_.push_back(m);
            return (int)propMeshes_.size() - 1;
        };
        std::vector<float> v;
        // 0: round tree
        cylinder(v, vec3(0, 1.1f, 0), 1, 0.2f, 2.4f, 7, S_TRUNK, 1.f, 0.f, 0.25f);
        blob(v, vec3(0, 3.2f, 0), vec3(1.55f, 1.35f, 1.5f), 1, S_LEAF, S_LEAF2, 0.12f, 1, 0.3f, 1.f, 1.8f, 4.6f);
        blob(v, vec3(0.75f, 2.55f, 0.35f), vec3(0.95f, 0.85f, 0.95f), 1, S_LEAF, S_LEAF2, 0.12f, 2, 0.3f, 0.8f, 1.8f, 4.6f);
        const int roundTree = addMesh(v);
        v.clear();
        // 1: cypress
        cylinder(v, vec3(0, 0.5f, 0), 1, 0.18f, 1.2f, 6, S_TRUNK, 1.f, 0.f, 0.1f);
        blob(v, vec3(0, 3.0f, 0), vec3(0.85f, 2.6f, 0.85f), 1, S_LEAF2, S_LEAF, 0.1f, 3, 0.1f, 1.f, 0.6f, 5.6f);
        const int cypress = addMesh(v);
        v.clear();
        // 2: pine
        cylinder(v, vec3(0, 0.55f, 0), 1, 0.2f, 1.3f, 6, S_TRUNK, 1.f, 0.f, 0.1f);
        cone(v, vec3(0, 1.0f, 0), 1.55f, 2.0f, 9, S_LEAF, S_LEAF2, 0.2f, 0.5f);
        cone(v, vec3(0, 2.25f, 0), 1.2f, 1.8f, 9, S_LEAF, S_LEAF2, 0.45f, 0.8f);
        cone(v, vec3(0, 3.4f, 0), 0.85f, 1.6f, 9, S_LEAF, S_LEAF2, 0.7f, 1.f);
        const int pine = addMesh(v);
        v.clear();
        // 3: rock
        blob(v, vec3(0, 0.15f, 0), vec3(1.f, 0.62f, 0.85f), 1, S_ROCK, S_ROCK, 0.22f, 4, 0, 0, 0, 1);
        const int rock = addMesh(v);
        v.clear();
        // 4: bush
        blob(v, vec3(0, 0.35f, 0), vec3(0.9f, 0.7f, 0.85f), 1, S_LEAF, S_LEAF2, 0.15f, 5, 0.1f, 0.4f, 0.f, 1.f);
        const int bush = addMesh(v);
        v.clear();
        // 5: cactus-like column (mesa)
        cylinder(v, vec3(0, 1.4f, 0), 1, 0.38f, 2.8f, 8, S_LEAF2, 1.f, 0.f, 0.05f);
        cylinder(v, vec3(0.62f, 1.9f, 0), 1, 0.26f, 1.2f, 8, S_LEAF2, 1.f, 0.f, 0.05f);
        cylinder(v, vec3(0.38f, 1.4f, 0), 0, 0.2f, 0.6f, 8, S_LEAF2, 1.f, 0.f, 0.05f);
        const int cactus = addMesh(v);
        v.clear();
        // 6: palm: a leaning ringed trunk and a crown of drooping fronds.
        {
            vec3 p(0, 0, 0);
            for (int k = 0; k < 6; k++) {
                vec3 q = p + vec3(0.12f + 0.03f * k, 0.8f, 0);
                cylinder(v, (p + q) * 0.5f, 1, 0.2f - 0.015f * k, 0.86f, 7, S_TRUNK, k % 2 ? 0.9f : 1.f, k * 0.12f, (k + 1) * 0.12f);
                p = q;
            }
            for (int l = 0; l < 7; l++) {
                const float a = TAU * l / 7 + 0.3f;
                const vec3 d(std::cos(a), 0, std::sin(a));
                const vec3 side(-d.z, 0, d.x);
                const int SEGS = 5;
                for (int k = 0; k < SEGS; k++) {
                    auto at = [&](int kk) {
                        float u = kk / (float)SEGS;
                        return p + d * (u * 2.4f) + vec3(0, 0.5f * u - 1.3f * u * u, 0);
                    };
                    auto wid = [&](int kk) { float u = kk / (float)SEGS; return 0.42f * std::sin(PI * (0.15f + 0.85f * u)) + 0.02f; };
                    vec3 a0 = at(k), a1 = at(k + 1);
                    vec3 l0 = a0 - side * wid(k), r0 = a0 + side * wid(k), l1 = a1 - side * wid(k + 1), r1 = a1 + side * wid(k + 1);
                    const float sl = l % 2 ? (float)S_LEAF : (float)S_LEAF2;
                    flatTri(v, l0, r0, r1, sl, 1.f, 1.f, 1.f, 1.f, vec3(0, 1, 0));
                    flatTri(v, l0, r1, l1, sl, 1.f, 1.f, 1.f, 1.f, vec3(0, 1, 0));
                }
            }
        }
        const int palm = addMesh(v);
        const int meshOf[P_KIND_COUNT] = {roundTree, cypress, pine, rock, bush, cactus, palm};
        // One layout for every variant; each fills it with its own tree shapes.
        enum { BIG, SMALL, ACCENT, STONE };
        struct Spot { int role; vec3 pos; float scale, yaw; };
        const float G = GROUND_Y;
        const Spot spots[] = {
            {BIG, vec3(-12.8f, G, -2.6f), 1.15f, 0.3f}, {SMALL, vec3(-11.3f, G, 2.4f), 0.75f, 1.7f},
            {ACCENT, vec3(12.9f, G, -2.8f), 1.05f, 0.f}, {SMALL, vec3(11.6f, G, -4.4f), 0.7f, 0.8f},
            {BIG, vec3(12.1f, G, 2.2f), 0.85f, 2.1f}, {ACCENT, vec3(-14.1f, G, -0.1f), 0.6f, 2.f},
            {STONE, vec3(-13.9f, G - 0.4f, 3.8f), 0.8f, 0.6f}, {STONE, vec3(14.2f, G - 0.7f, 4.4f), 0.6f, 2.f},
            {STONE, vec3(7.0f, G - 0.15f, 3.4f), 0.4f, 1.f},
            {BIG, vec3(-36.f, WATER_Y + 1.4f, -32.f), 0.9f, 0.f}, {ACCENT, vec3(43.f, WATER_Y + 2.2f, -40.f), 1.1f, 0.f},
        };
        for (int vi = 0; vi < VARIANT_COUNT; vi++)
            for (const Spot& sp : spots) {
                const Variant& V = VARIANTS[vi];
                int kind = sp.role == BIG ? V.big : sp.role == SMALL ? V.small : sp.role == ACCENT ? V.accent : P_ROCK;
                float sc = sp.scale;
                if (kind == P_BUSH && sp.role != STONE) sc *= 1.1f;
                if (kind == P_PALM) sc *= 1.05f;
                props_.push_back({meshOf[kind], sp.pos, sc, sp.yaw, vi});
            }
    }

    // ---- Water plane.
    {
        const float q[18] = {-600, WATER_Y, -700, 600, WATER_Y, -700, 600, WATER_Y, 300,
                             -600, WATER_Y, -700, 600, WATER_Y, 300, -600, WATER_Y, 300};
        glGenVertexArrays(1, &waterMesh_.vao);
        glGenBuffers(1, &waterMesh_.vbo);
        glBindVertexArray(waterMesh_.vao);
        glBindBuffer(GL_ARRAY_BUFFER, waterMesh_.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindVertexArray(0);
        waterMesh_.count = 6;
    }
}

// HUD font: Comfortaa (rounded, friendly), baked as a signed distance field so it scales cleanly and gets soft
// shadows for free.
void DioramaUniverse::buildFont() {
    if (!stbtt_InitFont(&gFont, FONT_TTF, stbtt_GetFontOffsetForIndex(FONT_TTF, 0))) {
        std::fprintf(stderr, "[gl] font init failed\n");
        return;
    }
    fontPx_ = 56.f;
    fontScaleTT_ = stbtt_ScaleForPixelHeight(&gFont, fontPx_);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&gFont, &asc, &desc, &gap);
    fontAscent_ = asc * fontScaleTT_;
    const int AW = 1024, AH = 512, PAD = 9;
    std::vector<unsigned char> atlas(AW * AH, 0);
    int x = 1, y = 1, rowH = 0;
    for (int ch = 32; ch < 128; ch++) {
        Glyph& g = glyphs_[ch - 32];
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&gFont, ch, &adv, &lsb);
        g.adv = adv * fontScaleTT_;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(&gFont, fontScaleTT_, ch, PAD, 128, 128.f / PAD, &w, &h, &xo, &yo);
        if (!sdf) { g.w = g.h = 0; continue; }
        if (x + w + 1 > AW) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + h + 1 > AH) { stbtt_FreeSDF(sdf, nullptr); break; }
        for (int r = 0; r < h; r++) std::memcpy(&atlas[(y + r) * AW + x], sdf + r * w, w);
        g.x0 = x / (float)AW;
        g.y0 = y / (float)AH;
        g.x1 = (x + w) / (float)AW;
        g.y1 = (y + h) / (float)AH;
        g.ox = (float)xo;
        g.oy = (float)yo;
        g.w = (float)w;
        g.h = (float)h;
        x += w + 1;
        rowH = std::max(rowH, h);
        stbtt_FreeSDF(sdf, nullptr);
    }
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    fontOk_ = true;
}

void DioramaUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    createTargets();
}

void DioramaUniverse::destroyTargets() {
    if (!msFbo_) return;
    glDeleteFramebuffers(1, &msFbo_);
    glDeleteRenderbuffers(1, &msColor_);
    glDeleteRenderbuffers(1, &msDepth_);
    glDeleteFramebuffers(1, &sceneFbo_);
    glDeleteTextures(1, &sceneTex_);
    glDeleteFramebuffers(1, &reflFbo_);
    glDeleteTextures(1, &reflTex_);
    glDeleteRenderbuffers(1, &reflDepth_);
    glDeleteFramebuffers(2, halfFbo_);
    glDeleteTextures(2, halfTex_);
    msFbo_ = 0;
}

DioramaUniverse::~DioramaUniverse() {
    destroyTargets();
    for (GLuint p : {progBlock_, progWorld_, progBlockDepth_, progWorldDepth_, progSky_, progWater_, progSprite_, progDown_,
                     progBlur_, progComp_, progPanel_, progText_})
        glDeleteProgram(p);
    for (GLuint a : {emptyVao_, spriteVao_, hudVao_}) glDeleteVertexArrays(1, &a);
    for (GLuint b : {instVbo_, quadVbo_, spriteVbo_, hudVbo_}) glDeleteBuffers(1, &b);
    auto freeMesh = [](Mesh& m) {
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
    };
    for (Mesh* m : {&cube_, &sphere_, &island_, &tower_, &waterMesh_}) freeMesh(*m);
    for (Mesh& m : propMeshes_) freeMesh(m);
    for (GLuint t : {occTex_, shadowTex_, fontTex_}) glDeleteTextures(1, &t);
    glDeleteFramebuffers(1, &shadowFbo_);
}

void DioramaUniverse::createTargets() {
    destroyTargets();
    glGenFramebuffers(1, &msFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glGenRenderbuffers(1, &msColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] MSAA FBO incomplete\n");

    auto makeTex = [](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    makeTex(sceneTex_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex_, 0);

    halfW_ = std::max(1, width_ / 2);
    halfH_ = std::max(1, height_ / 2);
    glGenFramebuffers(1, &reflFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, reflFbo_);
    makeTex(reflTex_, halfW_, halfH_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflTex_, 0);
    glGenRenderbuffers(1, &reflDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, reflDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, halfW_, halfH_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflDepth_);

    glGenFramebuffers(2, halfFbo_);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, halfFbo_[i]);
        makeTex(halfTex_[i], halfW_, halfH_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, halfTex_[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------------------------------------
// Scenes: the light moves to the new time of day; a new song's landscape blends in and its trees regrow.

// The song picks the place (variant); the phase picks the time of day.
void DioramaUniverse::setSong(const SongLook& look, float seconds) {
    seed_ = look.seed;
    int v = look.variant >= 0 ? look.variant % VARIANT_COUNT : variantOf(look);
    if (look.variant < 0 && v == look.avoidVariant) v = (v + 1 + (int)(look.seed % (VARIANT_COUNT - 1))) % VARIANT_COUNT;
    rng_ = Rng(look.seed ^ 0xD10ull);
    if (!hasSong_ || seconds <= 0.05f) {
        hasSong_ = true;
        worldFrom_ = worldTo_ = v;
        for (int i = 0; i < MAX_VARIANTS; i++) propGrow_[i] = i == v ? 1.f : 0.f;
        return;
    }
    startLight(todOf(level_, energy_), v, seconds, 0.f);
}

void DioramaUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level_ = level;
    energy_ = energy;
    const float tod = todOf(level, energy);
    if (!hasLevel_ || seconds <= 0.05f) {
        hasLevel_ = true;
        tod_ = todFrom_ = todTo_ = tod;
        transT_ = 1.f;
        return;
    }
    startLight(tod, worldTo_, seconds, impact);
}

// A new section or a level up: a breath of wind.
void DioramaUniverse::refresh(float strength) { gust_ = std::max(gust_, 0.5f * saturate(strength)); }

// The light moves to the new time of day; a new landscape blends in and its trees regrow.
void DioramaUniverse::startLight(float tod, int world, float seconds, float impact) {
    // Start from wherever the light is now (a transition can be retargeted midway without a jump).
    const float m = smoothstepf(0.f, 1.f, transT_);
    todFrom_ = tod_;
    if (worldFrom_ != worldTo_ && m < 1.f) worldFrom_ = m < 0.5f ? worldFrom_ : worldTo_;
    else worldFrom_ = worldTo_;
    worldTo_ = world;
    todTo_ = tod;
    transT_ = 0.f;
    transDur_ = std::max(0.3f, seconds);
    if (impact > 0.f) {
        gust_ = std::max(gust_, impact);
        flare_ = std::max(flare_, impact * 0.7f);
        if (impact >= 0.9f) rings_.push_back({0.f, 1.2f});
    }
}

vec3 DioramaUniverse::cellPos(float x, float y) const { return layout::cellPos(x, y); }

vec2 DioramaUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

// From afar: the day sky over pale clay, warm brown ink (the HUD's), the variant's T piece as the accent.
TransitionHints DioramaUniverse::hints() const {
    const Variant& V = VARIANTS[worldTo_];
    TransitionHints h;
    h.background = lerp(hex(V.sky[1][1]), hex(V.c[S_TOWER]), 0.4f);
    h.ink = hex(0x4D3B3E);
    h.accent = hex(V.piece[2]);
    h.light = true;
    if (inverted_ || V.night) { // night (a night place, or the slow motion)
        h.background = V.night ? lerp(hex(V.sky[1][1]), hex(V.sky[1][0]), 0.4f) : hex(NIGHT.horizon);
        h.ink = hex(0xFFF9EF);
        h.light = false;
    }
    h.styles = TS_REBUILD | TS_RIPPLE; // not TILES: scattered flipping cards over the island are too busy
    return h;
}

// ---------------------------------------------------------------------------------------------------------
// Effects.

void DioramaUniverse::spawnDust(vec3 pos, vec3 dir, int n, float strength) {
    for (int i = 0; i < n && parts_.size() < 3000; i++) {
        Particle p;
        p.kind = 0;
        p.pos = pos + vec3(rng_.range(-0.35f, 0.35f), rng_.range(-0.05f, 0.1f), rng_.range(-0.1f, 0.3f));
        vec3 d = normalize(dir + vec3(rng_.range(-0.6f, 0.6f), rng_.range(0.f, 0.4f), rng_.range(0.f, 0.6f)));
        p.vel = d * rng_.range(0.8f, 1.9f) * strength;
        p.color = lerp(env_.pal[S_TOWER], vec3(1.f, 0.95f, 0.88f), 0.5f);
        p.maxLife = p.life = rng_.range(0.8f, 1.4f);
        p.size = rng_.range(0.22f, 0.36f) * (0.8f + 0.3f * strength);
        p.rot = 0;
        p.rotV = 0;
        p.grav = -0.25f;
        p.drag = 2.6f;
        p.alpha = 0.55f;
        parts_.push_back(p);
    }
}

void DioramaUniverse::addRipple(float x, float z, float strength) {
    if (ripples_.size() >= 16) {
        size_t oldest = 0;
        for (size_t i = 1; i < ripples_.size(); i++)
            if (ripples_[i].age > ripples_[oldest].age) oldest = i;
        ripples_.erase(ripples_.begin() + oldest);
    }
    ripples_.push_back({x, z, 0.f, strength});
}

void DioramaUniverse::splash(vec3 pos, float strength) {
    addRipple(pos.x, pos.z, strength);
    for (int i = 0; i < 7 && parts_.size() < 3000; i++) {
        Particle p;
        p.kind = 5;
        float a = rng_.range(0.f, TAU);
        p.pos = vec3(pos.x, WATER_Y + 0.1f, pos.z);
        p.vel = vec3(std::cos(a) * rng_.range(0.8f, 2.f), rng_.range(3.f, 5.5f) * strength, std::sin(a) * rng_.range(0.8f, 2.f));
        p.color = lerp(env_.shallow, vec3(1.f), 0.6f);
        p.maxLife = p.life = rng_.range(0.6f, 0.9f);
        p.size = rng_.range(0.09f, 0.15f);
        p.rot = p.rotV = 0;
        p.grav = 14.f;
        p.drag = 0.3f;
        p.alpha = 0.9f;
        parts_.push_back(p);
    }
}

void DioramaUniverse::spawnBody(vec3 pos, int type, vec3 vel, float delay, float spin, float life) {
    if (bodies_.size() >= 400) return;
    Body b;
    b.pos = pos;
    b.vel = vel;
    b.axis = normalize(vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)));
    b.angle = 0;
    b.spin = spin;
    b.delay = delay;
    b.life = life;
    b.shrink = std::min(0.6f, life);
    b.scale = 1.f;
    b.type = type;
    b.wet = false;
    b.color = env_.piece[type];
    bodies_.push_back(b);
}

void DioramaUniverse::levelUp() {
    rings_.push_back({0.f, 1.5f});
    birdTimer_ = 0.f;
    gust_ = std::max(gust_, 0.6f);
}

void DioramaUniverse::onEvent(const GameEvent& ev, const Game& game) {
    switch (ev.type) {
    case GameEvent::Lock: {
        auto inPiece = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        bool dropped = false;
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H && game.cell(c.x, c.y).dropped) dropped = true;
        // One puff of dust pushed out from under the piece where it settles (along its bottom faces, outward
        // from their middle), bigger after a hard drop.
        std::vector<vec3> contact;
        for (auto& c : ev.cells)
            if (!inPiece(c.x, c.y + 1)) contact.push_back(cellPos((float)c.x, (float)c.y) + vec3(0, -0.5f, 0.35f));
        if (contact.empty()) break;
        float x0 = 1e9f, x1 = -1e9f, yb = 1e9f;
        for (auto& p : contact) { x0 = std::min(x0, p.x - 0.5f); x1 = std::max(x1, p.x + 0.5f); yb = std::min(yb, p.y); }
        const float xm = 0.5f * (x0 + x1);
        const int n = dropped ? 7 : 4;
        for (int i = 0; i < n; i++) {
            // spread along the lowest contact, the outer grains pushed out the most
            const float u = n > 1 ? (float)i / (n - 1) : 0.5f;
            const float x = lerpf(x0 + 0.2f, x1 - 0.2f, u), side = (x - xm) / std::max(0.5f, 0.5f * (x1 - x0));
            spawnDust(vec3(x, yb, 0.35f), vec3(side * 1.2f, 0.1f, 0.35f), 1, dropped ? 1.2f : 0.75f);
        }
        break;
    }
    case GameEvent::Clear: {
        // Cleared blocks pop off the tower toward the viewer and shrink away as they tumble (gone in ~0.5 s).
        const bool tetris = ev.count >= 4;
        for (auto& c : ev.cells) {
            vec3 p = cellPos((float)c.x, (float)c.y);
            float side = (c.x - 4.5f) / 4.5f;
            vec3 vel(side * rng_.range(2.0f, 3.6f) + rng_.range(-0.5f, 0.5f), rng_.range(4.5f, 7.5f) + (tetris ? 1.5f : 0.f),
                     rng_.range(4.f, 6.5f));
            spawnBody(p, c.type, vel, std::fabs(c.x - 4.5f) * 0.012f + rng_.range(0.f, 0.03f), rng_.range(4.f, 10.f), 0.45f);
        }
        rings_.push_back({0.f, 0.35f + 0.15f * ev.count});
        if (tetris) {
            // Confetti and petals burst into the light; the sun flares and a gust goes through the trees.
            float yc = 0;
            for (auto& c : ev.cells) yc += cellPos(0, (float)c.y).y;
            yc /= (float)ev.cells.size();
            for (int i = 0; i < 50 && parts_.size() < 3000; i++) {
                Particle p;
                p.kind = i % 3 == 0 ? 1 : 2;
                p.pos = vec3(rng_.range(-4.5f, 4.5f), yc + rng_.range(-1.5f, 1.5f), rng_.range(0.3f, 1.2f));
                float a = rng_.range(0.15f, PI - 0.15f);
                float sp = rng_.range(6.f, 13.f);
                p.vel = vec3(std::cos(a) * sp * 0.9f, std::sin(a) * sp, rng_.range(1.f, 5.f));
                p.color = p.kind == 1 ? lerp(env_.piece[i % 7], vec3(1.f, 0.92f, 0.94f), 0.55f) : env_.piece[i % 7];
                p.maxLife = p.life = rng_.range(1.1f, 1.7f);
                p.size = p.kind == 1 ? rng_.range(0.2f, 0.28f) : rng_.range(0.13f, 0.19f);
                p.rot = rng_.range(0.f, TAU);
                p.rotV = rng_.range(-6.f, 6.f);
                p.grav = 3.2f;
                p.drag = 1.5f;
                p.alpha = 1.f;
                parts_.push_back(p);
            }
            flare_ = 1.f;
            gust_ = std::max(gust_, 1.f);
            birdTimer_ = std::min(birdTimer_, 0.3f);
        }
        break;
    }
    case GameEvent::BonusReady: readyGlow_ = 1.f; break;
    case GameEvent::TopOut: {
        // Game over: the stack crumbles row by row from the top and falls into the water.
        int topRow = Game::H;
        for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
        for (auto& c : ev.cells) {
            vec3 p = cellPos((float)c.x, (float)c.y);
            vec3 vel(rng_.range(-1.5f, 1.5f) + (c.x - 4.5f) * 0.35f, rng_.range(0.5f, 2.5f), rng_.range(2.5f, 4.8f));
            spawnBody(p, c.type, vel, 0.25f + (c.y - topRow) * 0.075f + rng_.range(0.f, 0.12f), rng_.range(2.f, 6.f), 7.f);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Light of the moment.

void DioramaUniverse::computeEnv(float dt, const MusicState& music) {
    // Time of day, eased along the transition: the sun sweeps.
    transT_ = std::min(1.f, transT_ + dt / std::max(0.01f, transDur_));
    const float m = smoothstepf(0.f, 1.f, transT_);
    tod_ = lerpf(todFrom_, todTo_, m);
    const float wm = worldFrom_ == worldTo_ ? 1.f : m;
    night_ = approach(night_, inverted_ ? 1.f : 0.f, 2.2f, dt);
    for (int i = 0; i < MAX_VARIANTS; i++) propGrow_[i] = approach(propGrow_[i], i == worldTo_ ? 1.f : 0.f, wm > 0.5f ? 2.5f : 0.6f, dt);

    // Interpolate the two keys around tod_.
    int k = 0;
    while (k < 2 && tod_ > TOD_AT[k + 1]) k++;
    const float f = smoothstepf(0.f, 1.f, saturate((tod_ - TOD_AT[k]) / (TOD_AT[k + 1] - TOD_AT[k])));
    const Variant &VA = VARIANTS[worldFrom_], &VB = VARIANTS[worldTo_];
    // Night places (blended across a song change) and the bonus night.
    nightV_ = lerpf((float)VA.night, (float)VB.night, wm);
    const float n = smoothstepf(0.f, 1.f, night_);
    nightAll_ = std::max(nightV_, n);
    Light L = mixLight(lightOf(TOD[k]), lightOf(TOD[k + 1]), f);
    if (nightV_ > 0.f) {
        // A night place: the moon crosses the sky with the phases (rising on the left, high, low on the right),
        // brighter toward the peak.
        Light N = lightOf(NIGHT);
        const float u = saturate(tod_ / 2.6f);
        N.sunDir = normalize(vec3(lerpf(-0.6f, 0.6f, u), 0.45f + 0.3f * std::sin(u * PI), 0.62f));
        N.skySun = normalize(vec3(lerpf(-0.5f, 0.5f, u), 0.05f + 0.1f * std::sin(u * PI), -0.86f));
        N.sunI *= 0.9f + 0.25f * u;
        N.stars *= 1.f - 0.3f * u;
        N.cloud = 0.25f;
        L = mixLight(L, N, nightV_);
    }
    // The bonus: night (in a day place) or the still night (in a night place).
    L = mixLight(L, mixLight(lightOf(NIGHT), lightOf(STILL), nightV_), n);
    Env& e = env_;
    // Sky: the variant's own gradients (dawn / day / golden hour, or its three nights); dusk is shared by the day
    // places, a night place keeps its peak night.
    auto skyKey = [&](int key, int which) {
        auto one = [&](const Variant& V) { return key >= 3 && !V.night ? hex(which ? TOD[3].horizon : TOD[3].zenith) : hex(V.sky[std::min(key, 2)][which]); };
        return lerp(one(VA), one(VB), wm);
    };
    const Light B = mixLight(lightOf(NIGHT), lightOf(STILL), nightV_);
    e.zenith = lerp(lerp(skyKey(k, 0), skyKey(k + 1, 0), f), B.zenith, n);
    e.horizon = lerp(lerp(skyKey(k, 1), skyKey(k + 1, 1), f), B.horizon, n);
    e.sunCol = L.sun * L.sunI;
    e.skyCol = L.sky * L.skyI;
    e.groundCol = L.ground * L.groundI;
    e.shadowTint = L.shadowTint;
    e.sunFogCol = L.sunFog;
    // Mist leans blue at dawn (blue hour), warm toward the sun later.
    e.fogCol = lerp(e.horizon, e.zenith, lerpf(0.12f + 0.33f * (1.f - smoothstepf(0.f, 0.9f, tod_)), 0.3f, nightAll_));
    e.fogDensity = L.fog;
    e.mist = L.mist;
    e.sunGlow = L.glow * (1.f + 0.6f * flare_);
    e.rim = L.rim;
    e.cloud = L.cloud;
    e.stars = L.stars;
    e.lantern = L.lantern;
    e.exposure = L.exposure;
    e.sunDir = normalize(L.sunDir);
    e.skySunDir = normalize(L.skySun);
    // Music: the wind and the water carry the energy; the beat is felt, not hammered.
    const float E = music.energy;
    e.wind = L.wind * (0.55f + 0.8f * E) + 0.18f * music.beatPulse + 0.9f * gust_;
    e.wave = L.wave * (0.5f + 0.9f * E) + 0.25f * gust_;
    e.glint = (1.f + 0.6f * music.bandsFast[NUM_BANDS - 3] * E) * (1.f - 0.4f * nightAll_);
    // Landscape palette (blended across a song change).
    for (int i = 0; i < 16; i++) e.pal[i] = lerp(hex(VA.c[i]), hex(VB.c[i]), wm);
    e.shallow = lerp(hex(VA.shallow), hex(VB.shallow), wm);
    e.deep = lerp(hex(VA.deep), hex(VB.deep), wm);
    for (int i = 0; i < 7; i++) e.piece[i] = lerp(hex(VA.piece[i]), hex(VB.piece[i]), wm);
}

// ---------------------------------------------------------------------------------------------------------

void DioramaUniverse::simulate(const Game& game, const MusicState& music, float dt) {
    // The bonus slows the world down with the game.
    const float wdt = dt * lerpf(1.f, 0.35f, night_);
    worldTime_ += wdt;
    windTime_ += wdt * (0.7f + 0.9f * env_.wind);
    waterTime_ += wdt * (0.6f + 0.5f * music.energy);
    cloudTime_ += wdt * (0.5f + env_.wind);
    gust_ = approach(gust_, 0.f, 0.8f, dt);
    flare_ = approach(flare_, 0.f, 1.1f, dt);
    readyGlow_ = approach(readyGlow_, 0.f, 1.5f, dt);
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 6.f, dt);

    // Kicks: a ring spreads on the water from the island's shore.
    kick_ = approach(kick_, 0.f, 4.f, dt);
    lastKickT_ += dt;
    const float kv = std::max(music.kick, kick_);
    if (kv > 0.42f && kickEnv_ <= 0.42f && lastKickT_ > 0.3f) {
        if (rings_.size() >= 6) rings_.erase(rings_.begin());
        rings_.push_back({0.f, std::min(1.f, 0.35f + 0.6f * kv)});
        lastKickT_ = 0.f;
    }
    kickEnv_ = kv;
    for (size_t i = 0; i < rings_.size();) {
        rings_[i].age += wdt;
        if (rings_[i].age > 7.f) { rings_.erase(rings_.begin() + i); continue; }
        i++;
    }
    while (rings_.size() > 6) rings_.erase(rings_.begin());
    for (size_t i = 0; i < ripples_.size();) {
        ripples_[i].age += wdt;
        if (ripples_[i].age > 3.5f) { ripples_.erase(ripples_.begin() + i); continue; }
        i++;
    }

    // Tumbling blocks.
    for (size_t i = 0; i < bodies_.size();) {
        Body& b = bodies_[i];
        if (b.delay > 0.f) { b.delay -= dt; i++; continue; }
        b.life -= dt;
        if (!b.wet) {
            b.vel.y -= 24.f * dt;
            b.pos += b.vel * dt;
            b.angle += b.spin * dt;
            // Ground: the plinth, then the island's top.
            float ground = -1e9f;
            const float qx = (b.pos.x - ISLAND_X) / ISLAND_RX, qz = (b.pos.z - ISLAND_Z) / ISLAND_RZ;
            if (std::sqrt(qx * qx + qz * qz) < 0.82f) ground = GROUND_Y;
            if (std::fabs(b.pos.x) < 6.9f && b.pos.z > -1.9f && b.pos.z < 1.4f) ground = -10.5f;
            if (b.pos.y - 0.45f < ground && b.vel.y < 0.f) {
                b.pos.y = ground + 0.45f;
                b.vel.y = -b.vel.y * 0.32f;
                b.vel.x *= 0.75f;
                b.vel.z *= 0.75f;
                b.spin *= 0.6f;
                if (b.vel.y < 1.2f) b.vel.y = 0.f;
                if (rng_.chance(0.4f)) spawnDust(b.pos + vec3(0, -0.4f, 0), vec3(0, 0.3f, 0), 1, 0.6f);
            }
            if (b.vel.y == 0.f && b.pos.y - 0.45f <= ground + 0.01f) {
                b.vel.x *= std::exp(-1.5f * dt);
                b.vel.z *= std::exp(-1.5f * dt);
                b.spin *= std::exp(-3.f * dt);
                b.life = std::min(b.life, 1.6f);
            }
            if (b.pos.y < WATER_Y + 0.2f) {
                b.wet = true;
                splash(b.pos, 0.7f + 0.3f * rng_.uniform());
                b.vel = b.vel * 0.2f;
            }
        } else {
            b.vel.y = approach(b.vel.y, -1.4f, 3.f, dt);
            b.vel.x *= std::exp(-2.f * dt);
            b.vel.z *= std::exp(-2.f * dt);
            b.pos += b.vel * dt;
            b.angle += b.spin * 0.3f * dt;
        }
        if (b.life < b.shrink) b.scale = std::max(0.f, b.life / b.shrink);
        if (b.life <= 0.f || b.pos.y < WATER_Y - 3.f) { b = bodies_.back(); bodies_.pop_back(); continue; }
        i++;
    }

    // Particles.
    for (size_t i = 0; i < parts_.size();) {
        Particle& p = parts_[i];
        p.life -= wdt;
        if (p.life <= 0.f) { p = parts_.back(); parts_.pop_back(); continue; }
        p.vel = p.vel * std::exp(-p.drag * wdt) - vec3(0, p.grav, 0) * wdt;
        if (p.kind == 1 || p.kind == 2) p.vel.x += std::sin(worldTime_ * 2.3f + p.rot) * 1.2f * wdt + env_.wind * 0.6f * wdt;
        if (p.kind == 0) p.size += wdt * 0.25f;
        p.pos += p.vel * wdt;
        p.rot += p.rotV * wdt;
        if ((p.kind == 1 || p.kind == 2) && p.pos.y < WATER_Y) { p = parts_.back(); parts_.pop_back(); continue; }
        if (p.kind == 5 && p.pos.y < WATER_Y && p.vel.y < 0) { p = parts_.back(); parts_.pop_back(); continue; }
        i++;
    }

    // Leaves let go of the trees in the wind (golden hour, gusts).
    const float leafRate = std::max(0.f, env_.wind - 0.45f) * 3.f * (1.f - nightAll_);
    if (rng_.uniform() < leafRate * wdt && parts_.size() < 3000) {
        Particle p;
        p.kind = 1;
        float side = rng_.chance(0.5f) ? -1.f : 1.f;
        p.pos = vec3(side * rng_.range(11.f, 13.5f), GROUND_Y + rng_.range(2.5f, 5.f), rng_.range(-3.f, 2.5f));
        p.vel = vec3(rng_.range(0.5f, 2.f), rng_.range(-0.2f, 0.6f), rng_.range(0.2f, 1.2f));
        p.color = lerp(env_.pal[S_LEAF], env_.sunFogCol, 0.25f);
        p.maxLife = p.life = rng_.range(3.f, 5.f);
        p.size = rng_.range(0.14f, 0.2f);
        p.rot = rng_.range(0.f, TAU);
        p.rotV = rng_.range(-3.f, 3.f);
        p.grav = 0.6f;
        p.drag = 0.8f;
        p.alpha = 1.f;
        parts_.push_back(p);
    }

    // Birds cross the sky in daylight (and after a Tetris or a new level).
    birdTimer_ -= wdt;
    const float day = smoothstepf(0.4f, 0.9f, tod_) * (1.f - smoothstepf(2.2f, 2.5f, tod_)) * (1.f - nightAll_);
    if (birdTimer_ <= 0.f) {
        const bool forced = birdTimer_ > -0.5f && birdTimer_ < 0.f && false;
        (void)forced;
        birdTimer_ = rng_.range(14.f, 26.f);
        if (day > 0.3f) {
            const float dir = rng_.chance(0.5f) ? 1.f : -1.f;
            const vec3 start(-dir * 75.f, rng_.range(8.f, 14.f), rng_.range(-45.f, -30.f));
            const int n = rng_.irange(4, 7);
            for (int i = 0; i < n; i++) {
                Bird b;
                b.pos = start + vec3(-dir * (i * 2.2f + rng_.range(0.f, 1.f)), (i % 2) * 1.2f + rng_.range(-0.4f, 0.4f),
                                     rng_.range(-2.f, 2.f));
                b.vel = vec3(dir * rng_.range(8.f, 9.5f), rng_.range(0.f, 0.6f), 0);
                b.phase = rng_.range(0.f, TAU);
                birds_.push_back(b);
            }
        }
    }
    for (size_t i = 0; i < birds_.size();) {
        Bird& b = birds_[i];
        b.pos += b.vel * wdt;
        b.phase += wdt * 9.f;
        if (std::fabs(b.pos.x) > 90.f) { b = birds_.back(); birds_.pop_back(); continue; }
        i++;
    }
}

// ---------------------------------------------------------------------------------------------------------
// What to draw this frame: blocks (board, piece, ghost, previews, tumbling bodies), gauge beads, sprites.

void DioramaUniverse::collect(const Game& g, const MusicState& music, double time) {
    solid_.clear();
    beads_.clear();
    sprites_.clear();
    const float S = 0.95f;
    auto inst = [&](vec3 p, vec3 sc, vec3 col, float kind = 0, float mask = 0, vec4 q = vec4(0, 0, 0, 1), float glow = 0) {
        solid_.push_back({vec4(p, kind), vec4(sc, mask), q, vec4(col, glow)});
    };
    auto occupied = [&](int x, int y) { return x >= 0 && x < Game::W && y >= 0 && y < Game::H && g.cell(x, y).type >= 0; };

    // Pieces locked together share one flash value: squash each about its own bottom.
    struct Group { float flash; float bottom; };
    std::vector<Group> groups;
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0 || c.flash <= 0.f) continue;
            bool found = false;
            for (auto& gr : groups)
                if (gr.flash == c.flash) { gr.bottom = std::max(gr.bottom, (float)y); found = true; }
            if (!found) groups.push_back({c.flash, (float)y});
        }
    const float breath = 1.f - 0.035f * music.beatPulse * (0.4f + 0.6f * music.energy);
    unsigned char occ[22][12] = {};
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            vec3 p = cellPos((float)x, y - g.rowOffset(y));
            vec3 sc(S, S * breath, S);
            if (c.flash > 0.f) {
                const float age = (1.f - c.flash) * Game::LOCK_FX_SECONDS;
                if (age < 1.4f) {
                    // Squash and stretch: the piece lands, squashes about its base and springs back.
                    const float amp = c.dropped ? 0.22f : 0.13f;
                    const float w = std::exp(-6.5f * age) * std::cos(age * 19.f) * amp;
                    float bottom = (float)y;
                    for (auto& gr : groups)
                        if (gr.flash == c.flash) bottom = gr.bottom;
                    const float by = cellPos(0, bottom - g.rowOffset(y)).y - 0.5f;
                    const float sy = 1.f - w, sxz = 1.f + 0.55f * w;
                    p.y = by + (p.y - by) * sy;
                    sc = vec3(S * sxz, S * sy * breath, S * sxz);
                }
            }
            int mask = (occupied(x - 1, y) ? 1 : 0) | (occupied(x + 1, y) ? 2 : 0) | (occupied(x, y - 1) ? 4 : 0) |
                       (occupied(x, y + 1) ? 8 : 0);
            inst(p, sc, env_.piece[c.type], 0, (float)mask);
            const int ty = Game::H - 1 - y; // 0 at the bottom row
            if (ty >= 0 && ty < 20) occ[ty + 1][x + 1] = 255;
        }
    glBindTexture(GL_TEXTURE_2D, occTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 12, 22, GL_RED, GL_UNSIGNED_BYTE, occ);

    // The falling piece and its ghost (a tile pressed into the back wall).
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        auto inP = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            int mask = (inP(c[0] - 1, c[1]) ? 1 : 0) | (inP(c[0] + 1, c[1]) ? 2 : 0) | (inP(c[0], c[1] - 1) ? 4 : 0) |
                       (inP(c[0], c[1] + 1) ? 8 : 0);
            inst(cellPos(c[0] + dx, c[1] + dy), vec3(S), env_.piece[p.type], 0, (float)mask);
        }
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            g.pieceCells(gp, cells);
            for (auto& c : cells) {
                if (c[1] < Game::HIDDEN) continue;
                vec3 col = lerp(env_.pal[S_WALL], env_.piece[p.type], 0.55f);
                inst(cellPos((float)c[0], (float)c[1]) + vec3(0, 0, -0.45f), vec3(0.88f, 0.88f, 0.14f), col, 1);
            }
        }
    }

    // Previews: hold on the left stele, the next three on the right one; they turn gently in the light.
    auto preview = [&](int type, vec3 center, float sc, float phase, bool dim) {
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        const float yaw = 0.32f * std::sin((float)time * 0.45f + phase);
        const vec4 q = quatAxis(vec3(0, 1, 0), yaw);
        const float c = std::cos(yaw), s = std::sin(yaw);
        center.y += 0.06f * std::sin((float)time * 1.1f + phase);
        vec3 col = env_.piece[type];
        if (dim) col = lerp(col, vec3(luminance(col)), 0.7f) * 0.75f;
        for (auto& cc : cells) {
            vec3 o((cc[0] - (minx + maxx) * 0.5f) * sc, -(cc[1] - (miny + maxy) * 0.5f) * sc, 0);
            vec3 r(o.x * c, o.y, -o.x * s);
            inst(center + r, vec3(sc * S), col, 0, 0, q);
        }
    };
    if (g.holdType() >= 0) preview(g.holdType(), vec3(-8.6f, 7.2f, -0.12f), 0.62f, 0.f, g.holdUsed());
    for (int i = 0; i < 3; i++) preview(g.next(i), vec3(8.6f, 7.2f - i * 3.2f, -0.12f), i == 0 ? 0.62f : 0.5f, 1.3f + i * 2.1f, false);

    // Tumbling bodies.
    for (const Body& b : bodies_) {
        float sc = S * b.scale;
        if (b.delay > 0.f) sc *= 1.f + 0.1f * std::sin(std::min(1.f, b.delay * 8.f) * PI);
        inst(b.pos, vec3(sc), b.color, 0, 0, quatAxis(b.axis, b.angle));
    }

    // Gauge: twelve clay beads around the dial; lit beads glow like little lanterns.
    {
        const vec3 c = layout::GAUGE_CENTER;
        const int N = Game::BONUS_LINES;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        const float br = 0.5f + 0.5f * std::sin((float)time * TAU * 0.5f);
        const vec3 lit = hex(0xFFB867), off = env_.pal[S_TOWER] * 0.82f;
        for (int i = 0; i < N; i++) {
            const float ang = PI * 0.5f - TAU * (i + 0.5f) / N;
            const vec3 p = c + vec3(std::cos(ang), std::sin(ang), 0) * layout::GAUGE_R + vec3(0, 0, -0.2f);
            float fill = saturate(gaugeShown_ * N - i);
            float glow = fill * (ready ? 0.55f + 0.45f * br : active ? 0.75f : 0.45f) + readyGlow_ * 0.8f * fill;
            vec3 col = lerp(off, lit, fill);
            float sz = 0.36f * (1.f + 0.12f * fill * (ready ? br : 0.f));
            beads_.push_back({vec4(p, 2), vec4(vec3(sz), 0), vec4(0, 0, 0, 1), vec4(col, glow)});
        }
    }

    // ---- Sprites.
    auto spr = [&](vec3 p, float size, vec3 col, float a, float kind, float rot = 0, float aspect = 1) {
        sprites_.push_back({vec4(p, size), vec4(col, a), vec4(kind, rot, 0, aspect)});
    };
    for (const Particle& p : parts_) {
        const float t = 1.f - p.life / p.maxLife;
        float a = p.alpha * std::min(1.f, t * 8.f) * std::min(1.f, p.life / (p.maxLife * 0.35f));
        float aspect = p.kind == 2 ? std::cos(p.rot * 1.7f) : (p.kind == 1 ? 0.55f : 1.f);
        spr(p.pos, p.size, p.color, a, (float)p.kind, p.rot, aspect);
    }
    for (const Bird& b : birds_) spr(b.pos, 0.55f, vec3(0.25f, 0.24f, 0.3f), 0.85f, 4, 0, 0.55f * std::sin(b.phase));
    // Lanterns' halos and fireflies (dusk and night).
    const float L = env_.lantern;
    if (L > 0.02f) {
        const vec3 lc = hex(0xFFC27A);
        for (int s = -1; s <= 1; s += 2) spr(vec3(s * 5.42f, 11.42f, 0.2f), 1.6f, lc, 0.55f * L, 3);
        const int NF = 34;
        for (int i = 0; i < NF; i++) {
            const float h1 = ((splitmix64(i * 7 + 1) >> 40) & 0xFFFF) / 65535.f, h2 = ((splitmix64(i * 7 + 2) >> 40) & 0xFFFF) / 65535.f,
                        h3 = ((splitmix64(i * 7 + 3) >> 40) & 0xFFFF) / 65535.f;
            const float side = i % 2 ? 1.f : -1.f;
            const float t = worldTime_ * (0.15f + 0.2f * h2) + h1 * 40.f;
            vec3 base(side * (7.f + 7.5f * h1), GROUND_Y + 0.6f + 4.f * h2, -4.f + 8.5f * h3);
            vec3 p = base + vec3(std::sin(t * 1.3f) * 1.2f, std::sin(t * 0.9f + 1.f) * 0.7f, std::cos(t * 1.1f) * 1.f);
            const float blink = smoothstepf(0.2f, 0.9f, 0.5f + 0.5f * std::sin(worldTime_ * (1.2f + h3) + h1 * 20.f));
            spr(p, 0.32f, vec3(1.f, 0.88f, 0.45f), L * blink * 1.6f, 3);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Drawing.

void DioramaUniverse::setCommonUniforms(GLuint p) {
    const Env& e = env_;
    set3f(p, "uSunDir", e.sunDir);
    set3f(p, "uSkySunDir", e.skySunDir);
    set3f(p, "uSunCol", e.sunCol);
    set3f(p, "uSkyCol", e.skyCol);
    set3f(p, "uGroundCol", e.groundCol);
    set3f(p, "uShadowTint", e.shadowTint);
    set3f(p, "uFogCol", e.fogCol);
    set3f(p, "uSunFogCol", e.sunFogCol);
    set3f(p, "uCamPos", camPos_);
    set3f(p, "uZenith", e.zenith);
    set3f(p, "uHorizon", e.horizon);
    set1f(p, "uFogDensity", e.fogDensity);
    set1f(p, "uMist", e.mist);
    set1f(p, "uWaterY", WATER_Y);
    set1f(p, "uTime", worldTime_);
    set1f(p, "uNight", nightAll_);
    set1f(p, "uRim", e.rim);
    set1f(p, "uSunGlow", e.sunGlow);
    set1f(p, "uStars", e.stars);
    set1f(p, "uCloud", e.cloud);
    set1f(p, "uCloudTime", cloudTime_);
    setMat(p, "uLightVP", lightVP_);
    set1i(p, "uShadow", 1);
}

void DioramaUniverse::drawInstances(const Mesh& m, const std::vector<Inst>& inst, GLuint) {
    if (inst.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(Inst), inst.data(), GL_STREAM_DRAW);
    glBindVertexArray(m.vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, m.count, (GLsizei)inst.size());
}

void DioramaUniverse::drawWorld(GLuint prog, bool) {
    setMat(prog, "uModel", mat4::identity());
    glBindVertexArray(island_.vao);
    glDrawArrays(GL_TRIANGLES, 0, island_.count);
    glBindVertexArray(tower_.vao);
    glDrawArrays(GL_TRIANGLES, 0, tower_.count);
    for (const Prop& pr : props_) {
        const float grow = propGrow_[pr.world];
        if (grow < 0.01f) continue;
        const float s = pr.scale * smoothstepf(0.f, 1.f, grow);
        mat4 M = mat4::identity();
        const float c = std::cos(pr.yaw) * s, sn = std::sin(pr.yaw) * s;
        M.at(0, 0) = c; M.at(0, 2) = sn;
        M.at(1, 1) = s;
        M.at(2, 0) = -sn; M.at(2, 2) = c;
        M.at(0, 3) = pr.pos.x; M.at(1, 3) = pr.pos.y; M.at(2, 3) = pr.pos.z;
        setMat(prog, "uModel", M);
        glBindVertexArray(propMeshes_[pr.mesh].vao);
        glDrawArrays(GL_TRIANGLES, 0, propMeshes_[pr.mesh].count);
    }
}

// The diorama's geometry: world and blocks (depth only for the shadow map; mirrored for the reflection).
void DioramaUniverse::drawScene(bool depthOnly, bool mirror) {
    const mat4 VP = depthOnly ? lightVP_ : vp_;
    const mat4 M = mirror ? mirror_ : mat4::identity();
    const float clipY = mirror ? WATER_Y - 0.05f : -1e6f;
    GLuint pw = depthOnly ? progWorldDepth_ : progWorld_;
    glUseProgram(pw);
    if (!depthOnly) setCommonUniforms(pw);
    setMat(pw, "uVP", VP);
    setMat(pw, "uMirror", M);
    set1f(pw, "uClipY", clipY);
    set1f(pw, "uWind", env_.wind);
    set1f(pw, "uWindTime", windTime_);
    if (!depthOnly) {
        float pal[48];
        for (int i = 0; i < 16; i++) { pal[i * 3] = env_.pal[i].x; pal[i * 3 + 1] = env_.pal[i].y; pal[i * 3 + 2] = env_.pal[i].z; }
        glUniform3fv(U(pw, "uPal"), 16, pal);
        set1i(pw, "uOcc", 2);
        set1f(pw, "uLanternGlow", env_.lantern);
        set1f(pw, "uEmissive", 0.f);
    }
    drawWorld(pw, depthOnly);

    GLuint pb = depthOnly ? progBlockDepth_ : progBlock_;
    glUseProgram(pb);
    if (!depthOnly) setCommonUniforms(pb);
    setMat(pb, "uVP", VP);
    setMat(pb, "uMirror", M);
    set1f(pb, "uClipY", clipY);
    if (!depthOnly) set1f(pb, "uLanternGlow", lerpf(0.3f * nightV_, 0.85f, smoothstepf(0.f, 1.f, night_)));
    drawInstances(cube_, solid_, pb);
    drawInstances(sphere_, beads_, pb);
}

void DioramaUniverse::setCamera(float t) {
    // A long lens, slightly elevated, a quarter turn to the side, slowly swaying; during a change of universe it
    // glides to the layout's front view (the same lens, straight on, at the reference scale on the board plane).
    const float aspect = (float)width_ / height_;
    const float fov = 22.f * PI / 180.f;
    const float k = smoothstepf(0.f, 1.f, camK_);
    const layout::Camera ref = layout::camera(width_, height_);
    const float refVisH = 2.f * ref.dist * std::tan(ref.fovDeg * 0.5f * PI / 180.f);
    const float yaw = (0.16f + 0.025f * std::sin(t * 0.071f) + 0.01f * std::sin(t * 0.13f)) * (1.f - k);
    const float pitch = (0.115f + 0.012f * std::sin(t * 0.053f + 1.f)) * (1.f - k);
    const float visH = lerpf(std::max(27.6f, 25.5f / aspect), refVisH, k);
    const float dist = visH / (2.f * std::tan(fov * 0.5f));
    const vec3 target = lerp(vec3(0.f, -0.55f, 0.f), ref.target, k);
    focus_ = dist;
    camPos_ = target + vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * dist;
    view_ = mat4::lookAt(camPos_, target, vec3(0, 1, 0));
    proj_ = mat4::perspective(fov, aspect, 2.f, 2000.f);
    vp_ = proj_ * view_;
    camRight_ = vec3(view_.at(0, 0), view_.at(0, 1), view_.at(0, 2));
    camUp_ = vec3(view_.at(1, 0), view_.at(1, 1), view_.at(1, 2));
}

void DioramaUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    const bool paused = f.paused;
    computeEnv(paused ? 0.f : dt, music);
    if (!paused) simulate(game, music, dt);
    pauseFade_ = approach(pauseFade_, paused ? 1.f : (f.dim ? 0.55f : 0.f), paused ? 4.f : 1.5f, dt);

    // ---- Camera (rises with the bridge at once, settles back slowly after a change).
    const float want = smoothstepf(0.f, 0.25f, bridge_.amount);
    camK_ = std::max(want, approach(camK_, want, 1.2f, dt));
    const float aspect = (float)width_ / height_;
    const float t = (float)f.time;
    setCamera(t);
    const float dist = focus_;
    mirror_ = mat4::identity();
    mirror_.at(1, 1) = -1.f;
    mirror_.at(1, 3) = 2.f * WATER_Y;

    // Light: an orthographic sun covering the island.
    {
        const vec3 lt(0.f, -3.f, -1.f);
        const vec3 up = std::fabs(env_.sunDir.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
        const mat4 lv = mat4::lookAt(lt + env_.sunDir * 90.f, lt, up);
        const mat4 lp = mat4::ortho(-22.f, 22.f, -22.f, 22.f, 20.f, 180.f);
        lightVP_ = lp * lv;
    }

    collect(game, music, f.time);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);

    // ---- Shadow map.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.f);
    drawScene(true, false);
    glDisable(GL_POLYGON_OFFSET_FILL);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, occTex_);
    glActiveTexture(GL_TEXTURE0);

    auto drawSky = [&](bool mirrored) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glUseProgram(progSky_);
        setCommonUniforms(progSky_);
        setMat(progSky_, "uInvVP", inverse(vp_));
        set1f(progSky_, "uMirrored", mirrored ? 1.f : 0.f);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    };

    // ---- Reflection (half resolution): the diorama mirrored in the water plane.
    glBindFramebuffer(GL_FRAMEBUFFER, reflFbo_);
    glViewport(0, 0, halfW_, halfH_);
    glClear(GL_DEPTH_BUFFER_BIT);
    drawSky(true);
    glEnable(GL_CLIP_DISTANCE0);
    drawScene(false, true);
    glDisable(GL_CLIP_DISTANCE0);

    // ---- Main pass (MSAA HDR, view distance in alpha).
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, width_, height_);
    glClear(GL_DEPTH_BUFFER_BIT);
    drawSky(false);
    drawScene(false, false);
    // Water.
    {
        glUseProgram(progWater_);
        setCommonUniforms(progWater_);
        setMat(progWater_, "uVP", vp_);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, reflTex_);
        glActiveTexture(GL_TEXTURE0);
        set1i(progWater_, "uRefl", 3);
        set2f(progWater_, "uRes", (float)width_, (float)height_);
        float rp[64] = {}, rg[24] = {};
        for (size_t i = 0; i < ripples_.size() && i < 16; i++) {
            rp[i * 4] = ripples_[i].x; rp[i * 4 + 1] = ripples_[i].z; rp[i * 4 + 2] = ripples_[i].age; rp[i * 4 + 3] = ripples_[i].strength;
        }
        for (size_t i = 0; i < rings_.size() && i < 6; i++) { rg[i * 4] = rings_[i].age; rg[i * 4 + 1] = rings_[i].strength; }
        glUniform4fv(U(progWater_, "uRipples"), 16, rp);
        glUniform4fv(U(progWater_, "uRings"), 6, rg);
        set3f(progWater_, "uShallow", env_.shallow);
        set3f(progWater_, "uDeep", env_.deep);
        set4f(progWater_, "uIsland", ISLAND_X, ISLAND_Z, ISLAND_RX, ISLAND_RZ);
        set1f(progWater_, "uWave", env_.wave);
        set1f(progWater_, "uWaterTime", waterTime_);
        set1f(progWater_, "uGlint", env_.glint);
        glBindVertexArray(waterMesh_.vao);
        glDrawArrays(GL_TRIANGLES, 0, waterMesh_.count);
    }
    // Sprites (premultiplied; the alpha channel keeps the view distance).
    if (!sprites_.empty()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glUseProgram(progSprite_);
        setCommonUniforms(progSprite_);
        setMat(progSprite_, "uVP", vp_);
        set3f(progSprite_, "uRight", camRight_);
        set3f(progSprite_, "uUp", camUp_);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
        glBufferData(GL_ARRAY_BUFFER, sprites_.size() * sizeof(Sprite), sprites_.data(), GL_STREAM_DRAW);
        glBindVertexArray(spriteVao_);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sprites_.size());
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_DEPTH_TEST);

    // ---- Resolve, blur (depth of field), composite.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray(emptyVao_);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, halfFbo_[0]);
    glViewport(0, 0, halfW_, halfH_);
    glUseProgram(progDown_);
    set1i(progDown_, "uSrc", 0);
    set2f(progDown_, "uTexel", 1.f / width_, 1.f / height_);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(progBlur_);
    set1i(progBlur_, "uSrc", 0);
    for (int pass = 0; pass < 2; pass++) {
        glBindFramebuffer(GL_FRAMEBUFFER, halfFbo_[1]);
        glBindTexture(GL_TEXTURE_2D, halfTex_[0]);
        set2f(progBlur_, "uDir", 1.6f / halfW_, 0.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, halfFbo_[0]);
        glBindTexture(GL_TEXTURE_2D, halfTex_[1]);
        set2f(progBlur_, "uDir", 0.f, 1.6f / halfH_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progComp_);
    set1i(progComp_, "uScene", 0);
    set1i(progComp_, "uBlur", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, halfTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    set1f(progComp_, "uFocus", dist);
    set1f(progComp_, "uDof", 1.f);
    set1f(progComp_, "uExposure", env_.exposure * (1.f + 0.08f * flare_));
    set1f(progComp_, "uFade", f.fade);
    set1f(progComp_, "uDim", pauseFade_);
    set1f(progComp_, "uSat", lerpf(1.04f, 0.5f, pauseFade_));
    set1f(progComp_, "uTime", t);
    set1f(progComp_, "uAspect", aspect);
    set1f(progComp_, "uVignette", 0.22f + 0.1f * nightAll_);
    {
        // The sun's soft glow, where it is painted in the sky.
        vec4 c = vp_ * vec4(camPos_ + env_.skySunDir * 1000.f, 1.f);
        float amt = 0.f;
        vec2 uv{-10, -10};
        if (c.w > 0.f) {
            uv = {c.x / c.w * 0.5f + 0.5f, c.y / c.w * 0.5f + 0.5f};
            amt = (0.18f * smoothstepf(1.5f, 2.1f, tod_) * (1.f - nightAll_) + 0.25f * flare_) *
                  smoothstepf(-0.05f, 0.02f, env_.skySunDir.y);
        }
        set2f(progComp_, "uSunUV", uv.x, uv.y);
        set1f(progComp_, "uGlowAmt", amt);
        set3f(progComp_, "uGlowCol", env_.sunFogCol);
    }
    set3f(progComp_, "uLift", env_.shadowTint * 0.025f);
    set3f(progComp_, "uGain", vec3(1.f));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::vector<HudText> hud;
    std::vector<Card> cards;
    layoutHud(f.hud, hud, cards);
    drawHud(hud, cards, f.panels);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------------------------------------
// HUD: warm off-white painted cards with a soft shadow; text in Comfortaa. Text on a card is a deep warm
// brown; text over the scene is off-white with a soft shadow.

void DioramaUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<Card>& cardsIn,
                              const std::vector<HudRect>& panels) {
    const float hs = std::max(1.f, height_ / 900.f);
    // Dark stained wood plaques (no white panels), cream lettering, a warm gold accent.
    const vec3 CARD = hexS(0x4A3329), ACCENT_INK = hexS(0xC4603F), LIGHT = hexS(0xFFF5E4), ACCENT_LIGHT = hexS(0xFFCF8A),
               SHADOW = hexS(0x2A1C1E);
    auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::vector<float>& out) {
        const float q[24] = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1, x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
        out.insert(out.end(), q, q + 24);
    };
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);

    // Cards.
    glUseProgram(progPanel_);
    set2f(progPanel_, "uRes", (float)width_, (float)height_);
    // The host's panels (zenscene's menu) are plain rectangles; the diorama's cards are rounded, with a shadow.
    struct R { float x, y, w, h, alpha, round; bool accent, custom; vec3 color; };
    std::vector<R> rects;
    for (const HudRect& p : panels) rects.push_back({p.x, p.y, p.w, p.h, p.alpha, 0.f, p.accent, p.custom, p.color});
    for (const Card& c : cardsIn) rects.push_back({c.x, c.y, c.w, c.h, c.alpha, 1.f, c.accent, false, vec3(0.f)});
    for (const R& r : rects) {
        const float sh = r.round > 0.f ? 7.f * hs : 0.f;
        const float hw = r.w * 0.5f, hh = r.h * 0.5f;
        std::vector<float> v;
        const float m = sh * 2.f + 2.f;
        quad(r.x - m, r.y - m, r.x + r.w + m, r.y + r.h + m, -hw - m, -hh - m, hw + m, hh + m, v);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        set2f(progPanel_, "uHalf", hw, hh);
        set1f(progPanel_, "uRadius", r.round > 0.f ? std::min(std::min(hw, hh), 6.f * hs * r.round) : 0.f);
        set1f(progPanel_, "uShadow", sh);
        set1f(progPanel_, "uAlpha", r.round > 0.f ? std::min(1.f, r.alpha * 1.04f) : r.alpha);
        set1f(progPanel_, "uWood", r.round > 0.f && !r.accent ? hs : 0.f);
        set3f(progPanel_, "uColor", r.custom ? r.color : r.accent ? ACCENT_INK : CARD);
        set3f(progPanel_, "uShadowCol", SHADOW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
    }

    if (!fontOk_) return;
    glUseProgram(progText_);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    set1i(progText_, "uFont", 0);
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float size = h.scale * 9.6f;      // pixel size of the font
        const float k = size / fontPx_;
        const float track = size * 0.03f;       // a little air between letters
        auto glyphOf = [](char c) {
            unsigned char ch = (unsigned char)c;
            return (ch < 32 || ch >= 128) ? (unsigned char)'?' : ch;
        };
        float width = 0;
        for (size_t i = 0; i < h.text.size(); i++) {
            unsigned char ch = glyphOf(h.text[i]);
            width += glyphs_[ch - 32].adv * k + track;
            if (i + 1 < h.text.size()) width += stbtt_GetCodepointKernAdvance(&gFont, ch, glyphOf(h.text[i + 1])) * fontScaleTT_ * k;
        }
        width -= track;
        float x = h.x;
        if (h.center) x = h.x - width * 0.5f;
        else if (h.x < 0) x = width_ + h.x - width;
        const float baseline = h.y + size * 0.74f;
        std::vector<float> v;
        float pen = x;
        for (size_t i = 0; i < h.text.size(); i++) {
            unsigned char ch = glyphOf(h.text[i]);
            const Glyph& gl = glyphs_[ch - 32];
            if (gl.w > 0) {
                float x0 = pen + gl.ox * k, y0 = baseline + gl.oy * k;
                quad(x0, y0, x0 + gl.w * k, y0 + gl.h * k, gl.x0, gl.y0, gl.x1, gl.y1, v);
            }
            pen += gl.adv * k + track;
            if (i + 1 < h.text.size()) pen += stbtt_GetCodepointKernAdvance(&gFont, ch, glyphOf(h.text[i + 1])) * fontScaleTT_ * k;
        }
        if (v.empty()) continue;
        // On a card?
        const float cxm = x + width * 0.5f, cym = h.y + size * 0.4f;
        bool onCard = false;
        for (const R& r : rects)
            if (r.alpha > 0.3f && !r.accent && !r.custom && cxm > r.x && cxm < r.x + r.w && cym > r.y && cym < r.y + r.h) onCard = true;
        const vec3 col = h.accent ? ACCENT_LIGHT : LIGHT;
        const float bias = size < 16.f ? 0.03f : 0.f; // small text a touch bolder
        if (!onCard) {
            // Soft shadow: the same glyphs, offset down, widened and blurred through the distance field.
            std::vector<float> s = v;
            for (size_t i = 0; i < s.size(); i += 4) { s[i] += 0.6f * hs; s[i + 1] += 1.6f * hs + size * 0.03f; }
            glBufferData(GL_ARRAY_BUFFER, s.size() * sizeof(float), s.data(), GL_STREAM_DRAW);
            set3f(progText_, "uColor", SHADOW);
            set1f(progText_, "uAlpha", 0.42f * h.alpha);
            set1f(progText_, "uSoft", 0.14f);
            set1f(progText_, "uBias", 0.1f);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(s.size() / 4));
        }
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        set3f(progText_, "uColor", col);
        set1f(progText_, "uAlpha", h.alpha);
        set1f(progText_, "uSoft", 0.f);
        set1f(progText_, "uBias", bias);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
    }
}

float DioramaUniverse::textWidth(const std::string& text, float scale) const {
    if (!fontOk_ || text.empty()) return text.size() * scale * 6.f;
    const float size = scale * 9.6f, k = size / fontPx_, track = size * 0.03f;
    float w = 0;
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch >= 128) ch = '?';
        w += glyphs_[ch - 32].adv * k + track;
        if (i + 1 < text.size()) {
            unsigned char n = (unsigned char)text[i + 1];
            if (n < 32 || n >= 128) n = '?';
            w += stbtt_GetCodepointKernAdvance(&gFont, ch, n) * fontScaleTT_ * k;
        }
    }
    return w - track;
}


// ---------------------------------------------------------------------------------------------------------
// HUD layout: the host's lines (text, anchor, size) in the diorama's typography, on small dark wooden plaques: the song
// and the place in corner cards, the stats on a plaque on the left stele, the slot labels as pills, the
// celebrations, the controls and the dialogs on cards of their own.

void DioramaUniverse::layoutHud(const std::vector<HudText>& in, std::vector<HudText>& out, std::vector<Card>& cards) const {
    const float hs = std::max(1.f, height_ / 900.f);
    out.clear();
    cards.clear();
    struct Box { float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; float alpha = 0; bool any = false; };
    auto extend = [&](Box& b, const HudText& t) {
        const float w = textWidth(t.text, t.scale), x = ugl::hudLeft(t, w, width_);
        b.x0 = std::min(b.x0, x);
        b.x1 = std::max(b.x1, x + w);
        b.y0 = std::min(b.y0, t.y);
        b.y1 = std::max(b.y1, t.y + t.scale * 9.6f);
        b.alpha = std::max(b.alpha, t.alpha);
        b.any = true;
    };
    auto card = [&](const Box& b, float padX, float padY, float alpha, float minW = 0.f, bool accent = false) {
        if (!b.any) return;
        float x0 = b.x0 - padX, x1 = b.x1 + padX;
        if (x1 - x0 < minW) { const float c = (x0 + x1) * 0.5f; x0 = c - minW * 0.5f; x1 = c + minW * 0.5f; }
        cards.push_back({x0, b.y0 - padY, x1 - x0, b.y1 - b.y0 + 2.f * padY, alpha, accent});
    };
    auto pill = [&](const HudText& t, float alpha) {
        const float th = t.scale * 9.6f, w = textWidth(t.text, t.scale), x = ugl::hudLeft(t, w, width_);
        cards.push_back({x - th * 0.8f, t.y - th * 0.48f, w + th * 1.6f, th * 1.9f, 0.94f * std::min(1.f, alpha * 1.6f)});
    };
    const float tod = tod_;
    const int mi = tod < 0.5f ? 0 : tod < 1.5f ? 1 : tod < 2.3f ? 2 : 3;
    const bool nightPlace = VARIANTS[worldTo_].night != 0;
    const char* moment = night_ > 0.5f ? (nightPlace ? "STILL NIGHT" : "NIGHT") : nightPlace ? NIGHT_NAMES[mi] : TOD_NAMES[mi];

    Box song, place, stats, help, dialog, toast;
    for (size_t i = 0; i < in.size(); i++) {
        HudText t = in[i];
        if (t.alpha <= 0.01f || t.text.empty()) continue;
        for (size_t k; (k = t.text.find("\xC2\xB7")) != std::string::npos;) t.text.replace(k, 2, "-");
        switch (t.role) {
        case HUD_TITLE:
            if (t.text.size() > 60) t.text = t.text.substr(0, 57) + "...";
            t.scale *= 0.82f; t.x += 10 * hs; t.y += 4 * hs; t.alpha = std::min(1.f, t.alpha * 2.3f);
            extend(song, t);
            break;
        case HUD_INFO:
            t.scale *= 0.87f; t.x += 10 * hs; t.y += 4 * hs; t.alpha = std::min(1.f, t.alpha * 2.3f);
            extend(song, t);
            break;
        case HUD_SECTION:
            t.scale *= 0.92f; t.x += 10 * hs; t.alpha = 0.9f;
            extend(place, t);
            break;
        case HUD_UNIVERSE:
            t.text += std::string("  ") + moment;
            t.x += 10 * hs; t.alpha = std::min(1.f, 0.4f + t.alpha);
            extend(place, t);
            break;
        case HUD_LABEL: {
            const bool stat = i + 1 < in.size() && in[i + 1].role == HUD_VALUE;
            if (stat) {
                t.scale *= 0.68f; t.accent = true; t.alpha = 0.85f;
                extend(stats, t);
            } else {
                t.scale *= 0.875f;
                t.alpha = std::min(1.f, t.alpha * 1.35f);
                pill(t, t.alpha);
            }
            break;
        }
        case HUD_VALUE:
            t.scale *= 0.82f; t.alpha = std::min(1.f, t.alpha * 1.1f);
            extend(stats, t);
            break;
        case HUD_NOTICE:
            t.scale *= 0.85f;
            pill(t, t.alpha);
            break;
        case HUD_KEY: {
            const float th = t.scale * 9.6f, w = textWidth(t.text, t.scale);
            const float cw = std::max(th * 1.9f, w + th), cx = t.x, cy = t.y + th * 0.42f;
            cards.push_back({cx - cw * 0.5f, cy - th * 0.95f, cw, th * 1.9f, 0.95f});
            break;
        }
        case HUD_TOAST:
        case HUD_TOAST_SUB:
            t.alpha = std::min(1.f, t.alpha * 1.15f);
            extend(toast, t);
            break;
        case HUD_HELP:
            t.scale *= 0.92f; t.alpha = std::min(1.f, t.alpha * 1.4f);
            extend(help, t);
            break;
        case HUD_DIALOG:
            t.scale *= 0.85f;
            extend(dialog, t);
            break;
        default: break;
        }
        out.push_back(t);
    }
    card(song, 18 * hs, 10 * hs, 0.92f * song.alpha);
    card(place, 18 * hs, 10 * hs, 0.85f);
    card(stats, 14 * hs, 12 * hs, 0.94f, std::min(150 * hs, std::fabs(project(vec3(-6.85f, 0, -0.6f)).x - project(vec3(-10.35f, 0, -0.6f)).x)));
    card(toast, 24 * hs, 12 * hs, 0.95f * toast.alpha);
    card(help, 12 * hs, 10 * hs, 0.85f * std::min(1.f, help.alpha * 1.5f));
    card(dialog, 36 * hs, 22 * hs, 0.95f * dialog.alpha);
}

// ---------------------------------------------------------------------------------------------------------
// Registry

const UniverseEntry& universe_diorama() {
    static const UniverseEntry e{
        "DIORAMA", "a sunlit clay miniature on an island", [] { return std::unique_ptr<Universe>(new DioramaUniverse()); },
        // Bright, warm songs suit the little world best; anything can play there.
        [](const SongLook& l) { return 0.7f + 0.5f * l.brightness + 0.3f * (l.minor ? 0.f : 1.f); },
        [] {
            std::vector<std::string> v;
            for (const Variant& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
