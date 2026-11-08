// The scene file reader (sandbox/scene_file.c): the packaged scenes read and mean what the code they replace did, and each rule of
// docs/specs/scene_file.md gives its error. The tests try the format with text of their own; only the packaged files are the files.
#include <sandbox/sandbox.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static SceneDesc desc; // large: not on the stack

static SceneRead read_text(const char* text)
{
    return scene_read(&desc, "t.nvscene", text, strlen(text));
}

// The text must fail, and its first error must say `expected`.
static void expect_error_at(int line, const char* text, const char* expected)
{
    SceneRead read = read_text(text);
    if (read.ok || !strstr(read.first_error, expected)) {
        printf("FAILED line %d: expected an error with '%s', got %s '%s'\n", line, expected, read.ok ? "ok" : "error", read.first_error);
        ++failures;
    }
}
#define EXPECT_ERROR(text, expected) expect_error_at(__LINE__, text, expected)

static void expect_ok_at(int line, const char* text)
{
    SceneRead read = read_text(text);
    if (!read.ok) {
        printf("FAILED line %d: expected ok, got '%s'\n", line, read.first_error);
        ++failures;
    }
}
#define EXPECT_OK(text) expect_ok_at(__LINE__, text)

// A smallest good file, in pieces the cases change one at a time.
#define HEAD "nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 5\n"
#define CAM "node cam\n    camera\n        projection perspective\n        fov 45\n        near 0.1\n        far 100\n"
#define CUBE "model cube\n    box 0 0 0 0.5 0.5 0.5\n"
#define CHARACTER "model person\n    file \"/a.glb\"\n    clips \"/c.glb\"\n"

static char* read_file(const char* path, size_t* size)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc((size_t)length + 1);
    *size = fread(text, 1, (size_t)length, file);
    text[*size] = 0;
    fclose(file);
    return text;
}

static void test_showcase(void)
{
    size_t size;
    char* text = read_file(SCENE_DATA_DIR "/showcase.nvscene", &size);
    CHECK(text != NULL);
    if (!text)
        return;
    SceneRead read = scene_read(&desc, "showcase.nvscene", text, size);
    if (!read.ok)
        printf("%s\n", read.first_error);
    CHECK(read.ok);
    free(text);
    if (!read.ok)
        return;

    CHECK(strcmp(desc.title, "Showcase") == 0);
    // The tree is the one the code built (the save's layout number hashes its names): camera, sun, ground, planet > moon,
    // character > sword, look target.
    CHECK(desc.node_count == 8);
    const char* names[8] = {"camera", "sun", "ground", "planet", "moon", "character", "sword", "look target"};
    for (u32 i = 0; i < 8 && i < desc.node_count; ++i)
        CHECK(strcmp(desc.nodes[i].name, names[i]) == 0);
    CHECK(desc.nodes[4].parent == 3 && desc.nodes[6].parent == 5 && desc.nodes[3].parent == -1 && desc.nodes[7].parent == -1);
    CHECK(desc.model_count == 5);

    // The values are the code's, as f32.
    CHECK(desc.nodes[0].camera.projection == NV_PROJECTION_PERSPECTIVE);
    CHECK(desc.nodes[0].camera.fov_y > 0.78539f && desc.nodes[0].camera.fov_y < 0.78540f);
    CHECK(desc.nodes[0].camera.near_z == 0.05f && desc.nodes[0].camera.far_z == 100.0f);
    CHECK(desc.nodes[1].has_light && desc.nodes[1].light.type == NV_LIGHT_DIRECTIONAL && desc.nodes[1].light.intensity == 1.1f);
    CHECK(desc.nodes[1].light.color.x == 1.0f && desc.nodes[1].light.color.y == 0.96f && desc.nodes[1].light.color.z == 0.9f);
    CHECK(desc.nodes[1].rotation.x == -0.377312273f && desc.nodes[1].rotation.w == 0.892427444f);
    CHECK(desc.nodes[3].position.x == -4.5f && desc.nodes[3].position.y == 1.6f && desc.nodes[3].position.z == -4.0f);
    CHECK(desc.nodes[4].position.x == 2.4f && desc.nodes[4].scale.x == 0.45f);
    CHECK(desc.nodes[3].has_color && desc.nodes[3].color[0] == 0.95f && desc.nodes[3].color[1] == 0.55f && desc.nodes[3].color[2] == 0.25f);
    CHECK(desc.nodes[2].model_index == 1 && desc.models[1].shapes[0].kind == SHAPE_PLANE && desc.models[1].shapes[0].value[0] == 30.0f);
    // The sword: three boxes in one model, attached to the hand, with the offset the rest pose gave.
    CHECK(desc.nodes[6].model_index == 2 && desc.models[2].shape_count == 3 && strcmp(desc.nodes[6].attach, "hand_r") == 0);
    CHECK(desc.models[2].shapes[2].value[1] == 0.52f && desc.models[2].shapes[2].value[4] == 0.41f);
    CHECK(desc.nodes[6].double_sided && desc.nodes[6].has_color && desc.nodes[6].color[0] == 0.75f);
    CHECK(desc.nodes[6].position.x == -0.0199999809f && desc.nodes[6].position.z == 1.49011612e-08f);
    CHECK(desc.nodes[6].rotation.x == -0.50000006f && desc.nodes[6].rotation.w == -0.49999994f);
    CHECK(desc.nodes[6].scale.y == 0.999999702f);
    CHECK(desc.nodes[7].position.y == 1.75f && desc.nodes[7].position.z == 1.2f);
    // The character: a file model with its clips and the root-motion copies.
    ModelDesc* character = &desc.models[4];
    CHECK(character->is_file && strcmp(character->file, "/assets/quaternius/character.glb") == 0);
    CHECK(strcmp(character->clips, "/assets/quaternius/clips.glb") == 0);
    CHECK(strcmp(character->root_motion.path, "/assets/quaternius/clips_rm.glb") == 0 && strcmp(character->root_motion.joint, "root") == 0);

    CHECK(desc.view.camera_index == 0 && desc.view.focus_index == 5 && desc.view.select_index == 5);
    CHECK(desc.view.yaw == 0.35f && desc.view.pitch == 0.12f && desc.view.distance == 5.0f);
    CHECK(desc.view.target.y == 0.92f);

    // The behaviors: the planet's and the moon's spin (0.7 and 2 rad/s), and the character's controls.
    CHECK(desc.behavior_count == 3);
    CHECK(desc.behaviors[0].kind == BEHAVIOR_SPIN && desc.behaviors[0].node == 3 && desc.behaviors[0].spin.speed == 0.7f);
    CHECK(desc.behaviors[0].spin.axis.y == 1.0f);
    CHECK(desc.behaviors[1].kind == BEHAVIOR_SPIN && desc.behaviors[1].node == 4 && desc.behaviors[1].spin.speed == 2.0f);
    CHECK(desc.behaviors[1].spin.axis.x > 0.70710 && desc.behaviors[1].spin.axis.x < 0.70711 && desc.behaviors[1].spin.axis.z == 0.0f);
    CharacterDesc* c = &desc.behaviors[2].character;
    CHECK(desc.behaviors[2].kind == BEHAVIOR_CHARACTER && desc.behaviors[2].node == 5);
    CHECK(strcmp(c->look_target[0], "Head") == 0 && strcmp(c->look_target[1], "look_target") == 0 && c->look_node_index == 7);
    CHECK(strcmp(c->jump.clip[0], "Jump_Start") == 0 && strcmp(c->jump.clip[2], "Jump_Land") == 0 && c->jump.air_time == 0.35f);
    CHECK(strcmp(c->clip, "Idle_Loop") == 0 && c->blend[0] == 0);
    // What the file leaves out is the table's fallback: speed 1, fade 0.3 s, turn 0.6 rad/s, the rest off.
    CHECK(c->speed == 1.0f && c->fade == 0.3f && c->turn == 0.6f && c->blend_weight == 0.0f && !c->root_motion && !c->look_at);
}

// The chess scene: its one model is a file, the board is scaled by 10, and it has the lighting its file names.
static void test_chess(void)
{
    size_t size;
    char* text = read_file(SCENE_DATA_DIR "/chess.nvscene", &size);
    CHECK(text != NULL);
    if (!text)
        return;
    SceneRead read = scene_read(&desc, "chess.nvscene", text, size);
    if (!read.ok)
        printf("%s\n", read.first_error);
    CHECK(read.ok);
    free(text);
    if (!read.ok)
        return;
    CHECK(strcmp(desc.title, "Chess") == 0);
    CHECK(desc.lighting.ambient[0] == 0.25f && desc.lighting.ambient[2] == 0.27f);
    s32 board = scene_find_node(&desc, "board");
    CHECK(board >= 0 && desc.nodes[board].model_index == 0 && desc.models[0].is_file);
    CHECK(board >= 0 && desc.nodes[board].scale.x == 10.0f && desc.nodes[board].scale.z == 10.0f);
    CHECK(scene_find_node(&desc, "camera") >= 0 && desc.view.camera_index == scene_find_node(&desc, "camera"));
}

static void test_good_files(void)
{
    EXPECT_OK(HEAD CAM);
    EXPECT_OK(HEAD CAM "\n\n   # a comment line\n");       // blank and comment lines are nothing
    EXPECT_OK("nvscene_version 1\r\ntitle \"T\"\r\nview\r\n    camera cam\r\n    yaw 0\r\n    pitch 0\r\n    distance 5\r\n" CAM);

    // Defaults: a node's name is its id, and it starts upright, unit size and visible.
    SceneRead read = read_text(HEAD CAM "node thing\n    position 1 2 3\n");
    CHECK(read.ok && desc.node_count == 2);
    NodeDesc* thing = &desc.nodes[1];
    CHECK(strcmp(thing->name, "thing") == 0 && thing->position.y == 2.0f && thing->rotation.w == 1.0f && thing->scale.x == 1.0f);
    CHECK(thing->visible == 1 && thing->model_index == -1 && !thing->has_color && !thing->has_camera && !thing->has_light);

    // Ids have a namespace for each kind of thing: a model and a node may share one.
    EXPECT_OK(HEAD "model cam\n    box 0 0 0 1 1 1\n" CAM "node x\n    model cam\n");

    // `name` takes quoted text with spaces; `euler` is degrees and becomes the rotation.
    read = read_text(HEAD CAM "node a\n    name \"two words\"\n    euler 90 0 0\n");
    CHECK(read.ok && strcmp(desc.nodes[1].name, "two words") == 0);
    CHECK(desc.nodes[1].rotation.x > 0.7071f && desc.nodes[1].rotation.x < 0.7072f && desc.nodes[1].rotation.w > 0.7071f);

    // The keys under `model` shape the node's own material.
    read = read_text(HEAD CUBE CAM "node a\n    model cube\n        color 1 0.5 0\n        double_sided 1\n        visible 0\n");
    CHECK(read.ok && desc.nodes[1].has_color && desc.nodes[1].color[1] == 0.5f && desc.nodes[1].double_sided && !desc.nodes[1].visible);
    CHECK(desc.nodes[1].model_index == 0);
    read = read_text(HEAD CUBE CAM "node a\n    model cube\n");
    CHECK(read.ok && !desc.nodes[1].has_color && !desc.nodes[1].double_sided && desc.nodes[1].visible);

    // Several shapes make one model; every kind of shape reads.
    read = read_text(HEAD "model many\n    box 0 0 0 1 1 1\n    plane 2 2\n    sphere 0 1 0 0.5 8 4\n    cylinder 0 0 0 1 1 8\n"
                     "    cone 0 0 0 1 1 8\n    capsule 0 0 0 0.5 0 8 2\n    torus 0 0 0 1 0.2 12 6\n" CAM);
    CHECK(read.ok && desc.models[0].shape_count == 7 && !desc.models[0].is_file);
    CHECK(desc.models[0].shapes[2].kind == SHAPE_SPHERE && desc.models[0].shapes[2].value[4] == 8.0f);
    CHECK(desc.models[0].shapes[6].kind == SHAPE_TORUS && desc.models[0].shapes[6].value[6] == 6.0f);

    // A point light, an orthographic camera.
    EXPECT_OK(HEAD CAM "node lamp\n    light\n        type point\n        range 10\n");
    EXPECT_OK(HEAD CAM "node lamp\n    light\n        type spot\n        range 10\n        cone 10 30\n        color 1 0 0\n        intensity 2\n");
    EXPECT_OK("nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 5\n"
              "node cam\n    camera\n        projection orthographic\n        height 4\n        near 0.1\n        far 100\n");
}

static void test_top_level(void)
{
    EXPECT_ERROR("title \"T\"\n", "must start with 'nvscene_version");
    EXPECT_ERROR("nvscene_version 2\n", "format 2 is newer than this build reads (1)");
    EXPECT_ERROR("nvscene_version 0\n", "'nvscene_version' must be 1 to 1");
    EXPECT_ERROR("nvscene_version\n", "'nvscene_version' takes one integer");
    EXPECT_ERROR("nvscene_version 1.5\n", "not an integer");
    EXPECT_ERROR("nvscene_version 1\ntitle T\n", "not text");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\ntitle \"U\"\n", "'title' is given twice");
    EXPECT_ERROR("nvscene_version 1\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 5\ntitle \"T\"\n", "'title' must come before 'view'");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\n", "missing 'view'");
    EXPECT_ERROR("nvscene_version 1\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 5\n", "missing 'title'");
    EXPECT_ERROR(HEAD, "no nodes");
    EXPECT_ERROR(HEAD CAM "view\n    camera cam\n", "'view' must come before 'node'");
    EXPECT_ERROR(HEAD CAM CUBE, "'model' must come before 'node'");
    EXPECT_ERROR(HEAD CAM "bogus\n    key 1\n", "unknown key 'bogus'");
    // The stress scene's blocks went with it (it is the stress sample now, docs/specs/samples.md).
    EXPECT_ERROR(HEAD CAM "benchmark\n    warmup 1\n", "unknown key 'benchmark'");
    EXPECT_ERROR(HEAD "palette colors\n    base 1 1 1\n" CAM, "unknown key 'palette'");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    yaw 0\n    pitch 0\n    distance 5\n" CAM, "block 'view' is missing 'camera'");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    pitch 0\n    distance 5\n" CAM, "block 'view' is missing 'yaw'");
    EXPECT_ERROR(HEAD "      extra 1\n" CAM, "inconsistent indentation");
    EXPECT_ERROR("nvscene_version 1\n  title \"T\"\n", "unexpected indentation");
    EXPECT_ERROR(HEAD CAM "node a\n    name \"x\"\n   position 0 0 0\n", "inconsistent indentation");
    EXPECT_ERROR(HEAD "\tnode a\n", "tab");
    EXPECT_ERROR("nvscene_version 1\ntitle \"open\n", "text is not closed");

    // The view names a node with a camera.
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera nowhere\n    yaw 0\n    pitch 0\n    distance 5\n" CAM, "no node 'nowhere'");
    EXPECT_ERROR(HEAD CAM "node plain\n" "view2\n", "unknown key 'view2'");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera plain\n    yaw 0\n    pitch 0\n    distance 5\nnode plain\n", "has no camera");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 5\n    focus x\n" CAM, "no node 'x'");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    yaw 400\n    pitch 0\n    distance 5\n" CAM,
                 "'yaw' must be >= -360 and <= 360");
    EXPECT_ERROR("nvscene_version 1\ntitle \"T\"\nview\n    camera cam\n    yaw 0\n    pitch 0\n    distance 0\n" CAM, "'distance' must be > 0");

    // Every error is counted, and the first one is kept.
    SceneRead read = read_text(HEAD CAM "node a\n    colour 1\n    position 1 2\n    position 1 2 3\n");
    CHECK(!read.ok && read.error_count == 3 && strstr(read.first_error, "t.nvscene:") == read.first_error);
    CHECK(strstr(read.first_error, "unknown key 'colour'") != NULL);
}

static void test_nodes(void)
{
    EXPECT_ERROR(HEAD CAM "node cam\n", "node 'cam' is defined twice");
    EXPECT_ERROR(HEAD CAM "node\n", "'node' takes an id");
    EXPECT_ERROR(HEAD CAM "node 9bad\n", "is not a name");
    EXPECT_ERROR(HEAD CAM "node a\n    position 1 2\n", "'position' takes 3 values");
    EXPECT_ERROR(HEAD CAM "node a\n    position 1 2 x\n", "is not a number");
    EXPECT_ERROR(HEAD CAM "node a\n    scale 0 1 1\n", "'scale' must be > 0");
    EXPECT_ERROR(HEAD CAM "node a\n    rotation 0 0 0 0\n", "'rotation' must not be 0 0 0 0");
    EXPECT_ERROR(HEAD CAM "node a\n    rotation 0 0 0 1\n    euler 0 0 0\n", "'rotation' and 'euler'");
    EXPECT_ERROR(HEAD CAM "node a\n    name x\n", "not text");
    EXPECT_ERROR(HEAD CAM "node a\n    name \"123456789012345678901234567890123\"\n", "text is longer than 31 bytes");
    EXPECT_ERROR(HEAD CUBE CAM "node a\n    model cube\n    model cube\n", "at most one 'model'");
    EXPECT_ERROR(HEAD CAM "node a\n    model nothing\n", "no model 'nothing'");
    EXPECT_ERROR(HEAD CUBE CAM "node a\n    model cube\n        glow 1\n", "unknown key 'glow'");
    EXPECT_ERROR(HEAD CUBE CAM "node a\n    model cube\n        color 2 0 0\n", "'color' must be >= 0 and <= 1");
    EXPECT_ERROR(HEAD CUBE CAM "node a\n    model cube\n        visible 2\n", "'visible' must be 0 or 1");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n        color 1 0 0\n", "'color' and 'double_sided' are for a shape model");
    EXPECT_ERROR(HEAD CAM "node a\n    attach hand\n", "'attach' needs a node with a file model above it");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    attach hand\n", "needs a node with a file model above it"); // above, not itself
    EXPECT_OK(HEAD CHARACTER CAM "node a\n    model person\n    node b\n        attach hand\n");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    node b\n        attach hand\n        attach foot\n", "at most one 'attach'");

    // Nodes nest as deep as the save's paths allow.
    char deep[2048] = HEAD CAM;
    for (int d = 0; d < SCENE_MAX_DEPTH; ++d) {
        char line[64];
        snprintf(line, sizeof(line), "%*snode n%d\n", d * 4, "", d);
        strcat(deep, line);
    }
    EXPECT_OK(deep);
    char deeper[2400];
    snprintf(deeper, sizeof(deeper), "%s%*snode n8\n", deep, SCENE_MAX_DEPTH * 4, "");
    EXPECT_ERROR(deeper, "nested too deep");

    // The limit on nodes.
    static char many[SCENE_MAX_NODES * 16 + 400];
    strcpy(many, HEAD);
    for (int n = 0; n < SCENE_MAX_NODES + 1; ++n) {
        char line[32];
        snprintf(line, sizeof(line), "node n%d\n", n);
        strcat(many, line);
    }
    EXPECT_ERROR(many, "too many nodes");
}

static void test_cameras_and_lights(void)
{
    EXPECT_ERROR(HEAD "node cam\n    camera\n        fov 45\n        near 0.1\n        far 100\n", "is missing 'projection'");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection perspective\n        near 0.1\n        far 100\n", "a perspective camera needs 'fov'");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection orthographic\n        near 0.1\n        far 100\n", "an orthographic camera needs 'height'");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection orthographic\n        fov 45\n        height 3\n        near 0.1\n        far 100\n",
                 "'fov' is for a perspective camera");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection perspective\n        fov 45\n        height 3\n        near 0.1\n        far 100\n",
                 "'height' is for an orthographic camera");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection perspective\n        fov 45\n        near 5\n        far 5\n", "'near' must be less than 'far'");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection fisheye\n        fov 45\n        near 1\n        far 5\n",
                 "unknown projection 'fisheye' (kinds: perspective, orthographic)");
    EXPECT_ERROR(HEAD "node cam\n    camera\n        projection perspective\n        fov 200\n        near 1\n        far 5\n", "'fov' must be");
    EXPECT_ERROR(HEAD CAM "    camera\n        projection perspective\n", "at most one 'camera'");

    EXPECT_ERROR(HEAD CAM "node l\n    light\n        color 1 1 1\n", "is missing 'type'");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type point\n", "a point or spot light needs 'range'");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type directional\n        range 5\n", "a directional light has no 'range'");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type spot\n        range 5\n", "a spot light needs 'cone'");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type point\n        range 5\n        cone 10 20\n", "'cone' is for a spot light");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type spot\n        range 5\n        cone 30 10\n", "the inner cone must be less than the outer");
    EXPECT_ERROR(HEAD CAM "node l\n    light\n        type sun\n", "unknown type 'sun'");
}

// `lighting` is optional, its ambient is 0.15 each without it, and it comes after the view and before the models.
static void test_lighting(void)
{
    SceneRead read = read_text(HEAD CAM);
    CHECK(read.ok && desc.lighting.ambient[0] == 0.15f && desc.lighting.ambient[1] == 0.15f && desc.lighting.ambient[2] == 0.15f);
    read = read_text(HEAD "lighting\n    ambient 0.3 0.25 0.2\n" CAM);
    CHECK(read.ok && desc.lighting.ambient[0] == 0.3f && desc.lighting.ambient[1] == 0.25f && desc.lighting.ambient[2] == 0.2f);
    EXPECT_OK(HEAD "lighting\n    ambient 0 0 0\n" CUBE CAM);
    EXPECT_ERROR(HEAD CUBE "lighting\n    ambient 0.3 0.3 0.3\n" CAM, "'lighting' must come before 'model'");
    EXPECT_ERROR(HEAD "lighting\n    ambient 0.3 0.3 0.3\n" "lighting\n    ambient 0 0 0\n" CAM, "'lighting' is given twice");
    EXPECT_ERROR(HEAD "lighting\n    ambient 1.5 0 0\n" CAM, "'ambient' must be");
    EXPECT_ERROR(HEAD "lighting\n    ambient 0.3 0.3\n" CAM, "'ambient' takes 3 values");
    EXPECT_ERROR(HEAD "lighting\n    ambient 0.3 0.3 0.3\n    sun 1\n" CAM, "unknown key 'sun'");
    EXPECT_ERROR(HEAD "lighting 1\n" CAM, "'lighting' takes no value");
}

static void test_models(void)
{
    EXPECT_ERROR(HEAD CUBE CUBE CAM, "model 'cube' is defined twice");
    EXPECT_ERROR(HEAD "model empty\n" CAM, "needs shape lines or a 'file'");
    EXPECT_ERROR(HEAD "model both\n    box 0 0 0 1 1 1\n    file \"/a.glb\"\n" CAM, "has shapes and a 'file'");
    EXPECT_ERROR(HEAD "model flat\n    box 0 0 0 1 1 1\n    clips \"/c.glb\"\n" CAM, "'clips' and 'root_motion_clips' go with a 'file'");
    EXPECT_ERROR(HEAD "model f\n    file \"/a.glb\"\n    file \"/b.glb\"\n" CAM, "'file' is given twice");
    EXPECT_ERROR(HEAD "model f\n    file /a.glb\n" CAM, "not text");
    EXPECT_ERROR(HEAD "model f\n    file \"/a.glb\"\n    root_motion_clips \"/r.glb\"\n" CAM, "'root_motion_clips' takes 2 values");
    EXPECT_ERROR(HEAD "model s\n    box 0 0 0 1 1\n" CAM, "'box' takes 6 values");
    EXPECT_ERROR(HEAD "model s\n    box 0 0 0 0 1 1\n" CAM, "'box' sizes must be above 0");
    EXPECT_ERROR(HEAD "model s\n    plane 1 -1\n" CAM, "'plane' sizes must be above 0");
    EXPECT_ERROR(HEAD "model s\n    sphere 0 0 0 1 2 8\n" CAM, "'sphere' needs 3 to 256");
    EXPECT_ERROR(HEAD "model s\n    sphere 0 0 0 1 8 1\n" CAM, "'sphere' needs 2 to 256");
    EXPECT_ERROR(HEAD "model s\n    sphere 0 0 0 1 8.5 4\n" CAM, "not an integer");
    EXPECT_ERROR(HEAD "model s\n    capsule 0 0 0 1 0.5 8 0\n" CAM, "'capsule' needs 1 to 256");
    EXPECT_ERROR(HEAD "model s\n    torus 0 0 0 1 0.2 12 2\n" CAM, "'torus' needs 3 to 256");
    EXPECT_ERROR(HEAD "model s\n    ball 1\n" CAM, "unknown key 'ball'");
    EXPECT_ERROR(HEAD "model s\n    sphere 0 0 0 1 100 100\n" CAM, "model 's' has 10201 vertices (at most 4096)");
    // Eight shapes fit; a ninth does not.
    char shapes[800] = HEAD "model s\n";
    for (int i = 0; i < SCENE_MAX_SHAPES; ++i)
        strcat(shapes, "    box 0 0 0 1 1 1\n");
    char eight[900];
    snprintf(eight, sizeof(eight), "%s%s", shapes, CAM);
    EXPECT_OK(eight);
    strcat(shapes, "    box 0 0 0 1 1 1\n");
    snprintf(eight, sizeof(eight), "%s%s", shapes, CAM);
    EXPECT_ERROR(eight, "too many shapes in a model");
}

static void test_behaviors(void)
{
    const char* with_spin = HEAD CAM "node a\n    spin\n        axis 0 2 0\n        speed 90\n";
    SceneRead read = read_text(with_spin);
    CHECK(read.ok && desc.behavior_count == 1 && desc.behaviors[0].spin.axis.y == 1.0f); // normalized
    CHECK(desc.behaviors[0].spin.speed > 1.5707f && desc.behaviors[0].spin.speed < 1.5708f); // 90 degrees a second, in radians

    EXPECT_ERROR(HEAD CAM "node a\n    spin\n        speed 90\n", "behavior 'spin' is missing 'axis'");
    EXPECT_ERROR(HEAD CAM "node a\n    spin\n        axis 0 0 0\n        speed 90\n", "'axis' must not be 0 0 0");
    EXPECT_ERROR(HEAD CAM "node a\n    spin\n        axis 0 1 0\n        speed 500\n", "'speed' must be");
    EXPECT_ERROR(HEAD CAM "node a\n    spin\n        axis 0 1 0\n        speed 1\n        wobble 1\n", "unknown key 'wobble'");
    EXPECT_ERROR(HEAD CAM "node a\n    spin\n        axis 0 1 0\n        speed 1\n    spin\n        axis 0 1 0\n        speed 1\n",
                 "at most one 'spin'");
    EXPECT_ERROR(HEAD CAM "node a\n    spin 1\n        axis 0 1 0\n        speed 1\n", "'spin' takes no value");

    // The character needs a file model with clips; look_at needs a look target.
    EXPECT_OK(HEAD CHARACTER CAM "node a\n    model person\n    character\n        look_target Head cam\n        look_at 1\n");
    EXPECT_ERROR(HEAD CUBE CAM "node a\n    model cube\n    character\n", "character: node 'a' needs a file model");
    EXPECT_ERROR(HEAD CAM "node a\n    character\n", "character: node 'a' needs a file model");
    EXPECT_ERROR(HEAD "model person\n    file \"/a.glb\"\n" CAM "node a\n    model person\n    character\n", "character: model 'person' has no 'clips'");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        look_at 1\n", "'look_at' needs a 'look_target'");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        look_target Head nobody\n", "no node 'nobody'");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        jump A B C 0\n", "'jump' must be");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        jump A B 0.3\n", "'jump' takes 4 values");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        speed 3\n", "'speed' must be");
    EXPECT_ERROR(HEAD CHARACTER CAM "node a\n    model person\n    character\n        root_motion 2\n", "'root_motion' must be 0 or 1");

    // The workload behaviors went with the stress scene.
    EXPECT_ERROR(HEAD CUBE CAM "node g\n    grid\n        model cube\n", "unknown key 'grid'");
}

int main(void)
{
    test_showcase();
    test_chess();
    test_good_files();
    test_top_level();
    test_nodes();
    test_cameras_and_lights();
    test_lighting();
    test_models();
    test_behaviors();
    if (!failures)
        printf("scene_file_test: ok\n");
    return failures ? 1 : 0;
}
