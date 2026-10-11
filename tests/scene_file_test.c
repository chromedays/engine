// The scene file reader (sandbox/scene_file.c): the packaged scenes read and mean what the code they replace did, and each rule of
// docs/specs/scene_file.md gives its error. The tests try the format with text of their own; only the packaged files are the files.
#include <sandbox/scene_file.h>

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
#define PALETTE "palette colors\n    base 0.6 0.6 0.6\n    wheel 10\n"
#define CHARACTER "model person\n    file \"/a.glb\"\n    clips \"/c.glb\"\n"
#define GRID_BODY                                                                  \
    "        model cube\n        palette colors\n        max 100\n        columns 10\n" \
    "        spacing 1\n        height 0.5\n        scale 1\n        count 10\n"

static f32 as_f32(u32 bits)
{
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

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
    CHECK(desc.model_count == 5 && desc.palette_count == 0);

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
    CHECK(!desc.has_benchmark);
}

static void test_stress(void)
{
    size_t size;
    char* text = read_file(SCENE_DATA_DIR "/stress.nvscene", &size);
    CHECK(text != NULL);
    if (!text)
        return;
    SceneRead read = scene_read(&desc, "stress.nvscene", text, size);
    if (!read.ok)
        printf("%s\n", read.first_error);
    CHECK(read.ok);
    free(text);
    if (!read.ok)
        return;

    CHECK(strcmp(desc.title, "Stress") == 0);
    CHECK(desc.node_count == 7 && desc.palette_count == 1 && desc.model_count == 3 && desc.behavior_count == 4);
    CHECK(desc.palettes[0].wheel == 200 && desc.palettes[0].base[0] == 0.6f && desc.palettes[0].base[2] == 0.66f);
    CHECK(desc.nodes[2].scale.x == 260.0f && desc.nodes[2].scale.y == 0.1f && desc.nodes[2].position.z == -50.0f);
    CHECK(desc.nodes[4].position.x == 14.0f && desc.nodes[4].position.y == 0.5f && desc.nodes[4].position.z == 8.0f);
    CHECK(desc.view.focus_index == 3 && desc.view.select_index == -1 && desc.view.distance == 28.0f);

    GridDesc* grid = &desc.behaviors[0].grid;
    CHECK(desc.behaviors[0].kind == BEHAVIOR_GRID && desc.behaviors[0].node == 3);
    CHECK(grid->max == 16000 && grid->columns == 128 && grid->spacing == 0.9f && grid->height == 0.25f && grid->scale == 0.5f);
    CHECK(grid->on == 1 && grid->count == 1000 && grid->colors_on == 0 && grid->colors == 16 && grid->churn_on == 0 && grid->churn == 32);
    CHECK(grid->model_index == 1 - 1 && grid->palette_index == 0);
    ChainDesc* chain = &desc.behaviors[1].chain;
    CHECK(chain->max == 1000 && chain->stride == 7 && chain->link.x == 0.12f && chain->link.y == 0.03f && chain->on == 0 && chain->count == 300);
    CHECK(chain->twist == 0.25f && chain->wobble == 0.04f && chain->model_index == 1);
    CrowdDesc* crowd = &desc.behaviors[2].crowd;
    CHECK(crowd->max == 200 && crowd->columns == 20 && crowd->spacing == 1.6f && crowd->offset == 0.37f && crowd->on == 1 && crowd->count == 16);
    CHECK(crowd->model_index == 2);
    EffectsDesc* effects = &desc.behaviors[3].effects;
    CHECK(effects->area[0] == 16.0f && effects->area[1] == 12.0f && effects->on == 0 && effects->particles == 0);

    // The benchmark: today's sixteen steps.
    CHECK(desc.has_benchmark && desc.warmup == 1.0f && desc.measure == 3.0f && desc.step_count == 16);
    CHECK(strcmp(desc.steps[0].name, "Cubes 250") == 0 && desc.steps[0].line_count == 1);
    CHECK(desc.steps[0].lines[0].behavior == 0 && as_f32(0) == 0.0f && desc.steps[0].lines[0].value == 250);
    const SceneField* count = &scene_behavior_def(BEHAVIOR_GRID)->fields[desc.steps[0].lines[0].field];
    CHECK(strcmp(count->key, "count") == 0 && count->parameter);
    CHECK(strcmp(desc.steps[10].name, "Particles 250k") == 0 && desc.steps[10].lines[0].behavior == 3 && desc.steps[10].lines[0].value == 250000);
    const SceneField* particles = &scene_behavior_def(BEHAVIOR_EFFECTS)->fields[desc.steps[10].lines[0].field];
    CHECK(strcmp(particles->key, "particles") == 0);
    CHECK(strcmp(desc.steps[15].name, "Beams 500") == 0 && desc.steps[15].lines[0].value == 500);
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

    // Ids have a namespace for each kind of thing: a model, a palette and a node may share one.
    EXPECT_OK(HEAD "palette cam\n    base 1 1 1\n    wheel 4\nmodel cam\n    box 0 0 0 1 1 1\n" CAM "node x\n    model cam\n");

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
    SceneRead twice = read_text(HEAD CAM "benchmark\n    warmup 1\n    measure 1\nbenchmark\n");
    CHECK(!twice.ok && twice.error_count == 2 && strstr(twice.first_error, "has no 'step' blocks") != NULL);
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

// `lighting` is optional, its ambient is 0.15 each without it, and it comes after the view and before the palettes.
static void test_lighting(void)
{
    SceneRead read = read_text(HEAD CAM);
    CHECK(read.ok && desc.lighting.ambient[0] == 0.15f && desc.lighting.ambient[1] == 0.15f && desc.lighting.ambient[2] == 0.15f);
    read = read_text(HEAD "lighting\n    ambient 0.3 0.25 0.2\n" CAM);
    CHECK(read.ok && desc.lighting.ambient[0] == 0.3f && desc.lighting.ambient[1] == 0.25f && desc.lighting.ambient[2] == 0.2f);
    EXPECT_OK(HEAD "lighting\n    ambient 0 0 0\n" PALETTE CAM);
    EXPECT_ERROR(HEAD PALETTE "lighting\n    ambient 0.3 0.3 0.3\n" CAM, "'lighting' must come before 'palette'");
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
    EXPECT_ERROR(HEAD PALETTE PALETTE CAM, "palette 'colors' is defined twice");
    EXPECT_ERROR(HEAD "palette p\n    base 0.5 0.5 0.5\n" CAM, "is missing 'wheel'");
    EXPECT_ERROR(HEAD "palette p\n    wheel 10\n" CAM, "is missing 'base'");
    EXPECT_ERROR(HEAD "palette p\n    base 2 0 0\n    wheel 10\n" CAM, "'base' must be");
    EXPECT_ERROR(HEAD "palette p\n    base 0 0 0\n    wheel 1\n" CAM, "'wheel' must be");
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

    // A node holds one generator.
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node a\n    grid\n" GRID_BODY "    chain\n        model cube\n", "at most one generator");

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

    // Grid: the fallbacks, and what a model needs.
    read = read_text(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY);
    CHECK(read.ok);
    GridDesc* grid = &desc.behaviors[0].grid;
    CHECK(grid->on == 1 && grid->colors_on == 0 && grid->churn_on == 0 && grid->churn == 32);
    CHECK(grid->colors == 9); // the fallback is 16, and a palette of 10 colors has 9 besides its base: cut while colors are off
    CHECK(grid->model_index == 0 && grid->palette_index == 0 && desc.behaviors[0].node == 1);
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "        colors_on 1\n", "palette 'colors' has 9 colors besides its base");
    EXPECT_OK(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "        colors_on 1\n        colors 9\n");
    EXPECT_ERROR(HEAD CUBE CAM "node g\n    grid\n        model cube\n        max 100\n        columns 10\n        spacing 1\n        height 0\n"
                 "        scale 1\n        count 10\n", "grid: model 'cube' is a shape model: it needs a 'palette'");
    EXPECT_ERROR(HEAD PALETTE CHARACTER CAM "node g\n    grid\n        model person\n        palette colors\n        max 100\n        columns 10\n"
                 "        spacing 1\n        height 0\n        scale 1\n        count 10\n", "is a file model, which has its own materials");
    EXPECT_OK(HEAD CHARACTER CAM "node g\n    grid\n        model person\n        max 100\n        columns 10\n        spacing 1\n"
              "        height 0\n        scale 1\n        count 10\n");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "        palette nothing\n", "'palette' is given twice");
    EXPECT_ERROR(HEAD CUBE CAM "node g\n    grid\n        model cube\n        palette nothing\n        max 100\n        columns 10\n"
                 "        spacing 1\n        height 0\n        scale 1\n        count 10\n", "grid: no palette 'nothing'");
    EXPECT_ERROR(HEAD PALETTE CAM "node g\n    grid\n        model nothing\n        palette colors\n        max 100\n        columns 10\n"
                 "        spacing 1\n        height 0\n        scale 1\n        count 10\n", "grid: no model 'nothing'");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "        count 101\n", "'count' is given twice");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n        model cube\n        palette colors\n        max 100\n        columns 10\n"
                 "        spacing 1\n        height 0\n        scale 1\n        count 101\n", "grid: 'count' is above 'max' (100)");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n        model cube\n        palette colors\n        columns 10\n        spacing 1\n"
                 "        height 0\n        scale 1\n        count 10\n", "behavior 'grid' is missing 'max'");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n        model cube\n        palette colors\n        max 100\n        columns 10\n"
                 "        spacing 1\n        height 0\n        scale 1\n", "behavior 'grid' is missing 'count'");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "        churn 300\n", "'churn' must be");

    // Chain and crowd.
    EXPECT_OK(HEAD PALETTE CUBE CAM "node c\n    chain\n        model cube\n        palette colors\n        max 10\n        link 1 0 0\n"
              "        twist 10\n        wobble 1\n        count 5\n");
    read = read_text(HEAD PALETTE CUBE CAM "node c\n    chain\n        model cube\n        palette colors\n        max 10\n        link 1 0 0\n"
                     "        twist 10\n        wobble 1\n        count 5\n");
    CHECK(read.ok && desc.behaviors[0].chain.stride == 1 && desc.behaviors[0].chain.on == 1 && desc.behaviors[0].chain.count == 5);
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node c\n    chain\n        model cube\n        palette colors\n        max 10\n        link 1 0 0\n"
                 "        twist 10\n        wobble 1\n        count 11\n", "chain: 'count' is above 'max' (10)");
    EXPECT_ERROR(HEAD PALETTE CUBE CAM "node c\n    chain\n        model cube\n        palette colors\n        max 10\n        link 1 0\n"
                 "        twist 10\n        wobble 1\n        count 5\n", "'link' takes 3 values");
    EXPECT_OK(HEAD CHARACTER CAM "node d\n    crowd\n        model person\n        max 10\n        columns 5\n        spacing 1\n        count 3\n");
    read = read_text(HEAD CHARACTER CAM "node d\n    crowd\n        model person\n        max 10\n        columns 5\n        spacing 1\n        count 3\n");
    CHECK(read.ok && desc.behaviors[0].crowd.offset == 0.0f && desc.behaviors[0].crowd.on == 1 && desc.behaviors[0].crowd.model_index == 0);
    EXPECT_ERROR(HEAD CUBE CAM "node d\n    crowd\n        model cube\n        max 10\n        columns 5\n        spacing 1\n        count 3\n",
                 "crowd: model 'cube' needs to be a file model with 'clips'");
    EXPECT_ERROR(HEAD "model person\n    file \"/a.glb\"\n" CAM "node d\n    crowd\n        model person\n        max 10\n        columns 5\n"
                 "        spacing 1\n        count 3\n", "needs to be a file model with 'clips'");
    EXPECT_ERROR(HEAD CHARACTER CAM "node d\n    crowd\n        model person\n        max 10\n        columns 5\n        spacing 1\n        count 201\n",
                 "'count' must be");
    EXPECT_ERROR(HEAD CHARACTER CAM "node d\n    crowd\n        model person\n        max 10\n        columns 5\n        spacing 1\n        count 11\n",
                 "crowd: 'count' is above 'max' (10)");

    // Effects.
    read = read_text(HEAD CAM "node e\n    effects\n        area 16 12\n        particles 1000\n");
    CHECK(read.ok && desc.behaviors[0].effects.on == 1 && desc.behaviors[0].effects.particles == 1000 && desc.behaviors[0].effects.area[1] == 12.0f);
    EXPECT_ERROR(HEAD CAM "node e\n    effects\n        particles 1\n", "behavior 'effects' is missing 'area'");
    EXPECT_ERROR(HEAD CAM "node e\n    effects\n        area 16 12\n        missiles 3000\n", "'missiles' must be");
}

static void test_benchmark(void)
{
    const char* base = HEAD PALETTE CUBE CAM "node g\n    grid\n" GRID_BODY "node e\n    effects\n        area 1 1\n";
    char text[1400];
    snprintf(text, sizeof(text), "%sbenchmark\n    warmup 1\n    measure 3\n    step \"Ten\"\n        g count 10\n"
             "    step \"Beams and cubes\"\n        e beams 7\n        g count 20\n", base);
    SceneRead read = read_text(text);
    CHECK(read.ok && desc.has_benchmark && desc.step_count == 2 && strcmp(desc.steps[1].name, "Beams and cubes") == 0);
    CHECK(desc.steps[1].line_count == 2 && desc.steps[1].lines[0].behavior == 1 && desc.steps[1].lines[0].value == 7);
    CHECK(desc.steps[1].lines[1].behavior == 0 && desc.steps[1].lines[1].value == 20);
    // A float parameter keeps its bits as a float.
    snprintf(text, sizeof(text), "%sbenchmark\n    warmup 0.5\n    measure 2\n    step \"x\"\n        g on 0\n", base);
    read = read_text(text);
    CHECK(read.ok && desc.warmup == 0.5f && desc.steps[0].lines[0].value == 0);

    const char* steps = "benchmark\n    warmup 1\n    measure 3\n";
    char with[1400];
#define STEP_CASE(body, expected)                                              \
    do {                                                                       \
        snprintf(with, sizeof(with), "%s%s%s", base, steps, body);             \
        expect_error_at(__LINE__, with, expected);                             \
    } while (0)
    STEP_CASE("    step \"x\"\n        nobody count 5\n", "unknown node 'nobody'");
    STEP_CASE("    step \"x\"\n        cam count 5\n", "node 'cam' has no generator");
    STEP_CASE("    step \"x\"\n        g max 5\n", "'max' is not a parameter of grid");
    STEP_CASE("    step \"x\"\n        g speed 5\n", "'speed' is not a parameter of grid");
    STEP_CASE("    step \"x\"\n        g count 101\n", "'count' is above the generator's 'max' (100)");
    STEP_CASE("    step \"x\"\n        g count 5.5\n", "not an integer");
    STEP_CASE("    step \"x\"\n        g count\n", "a step line is '<node id> <key> <value>'");
    STEP_CASE("    step \"x\"\n        g count 1\n        g count 2\n", "'count' is given twice in this step");
    STEP_CASE("    step \"x\"\n", "step 'x' has no lines");
    STEP_CASE("    step x\n        g count 1\n", "not text");
    STEP_CASE("    step\n", "'step' takes quoted text");
    STEP_CASE("    bogus 1\n", "unknown key 'bogus'");
    STEP_CASE("", "'benchmark' has no 'step' blocks");
    snprintf(with, sizeof(with), "%sbenchmark\n    measure 3\n    step \"x\"\n        g count 1\n", base);
    expect_error_at(__LINE__, with, "block 'benchmark' is missing 'warmup'");
    snprintf(with, sizeof(with), "%sbenchmark\n    warmup 1\n    measure 0\n    step \"x\"\n        g count 1\n", base);
    expect_error_at(__LINE__, with, "'measure' must be");

    // Four lines in a step and sixteen steps are the most.
    snprintf(with, sizeof(with), "%s%s    step \"x\"\n        g count 1\n        g on 1\n        g colors_on 1\n        g churn_on 1\n"
             "        g churn 3\n", base, steps);
    expect_error_at(__LINE__, with, "too many lines in a step");
    static char many[2600];
    snprintf(many, sizeof(many), "%s%s", base, steps);
    for (int i = 0; i < SCENE_MAX_STEPS + 1; ++i) {
        char step[64];
        snprintf(step, sizeof(step), "    step \"s%d\"\n        g count 1\n", i);
        strcat(many, step);
    }
    expect_error_at(__LINE__, many, "too many steps");
}

int main(void)
{
    test_showcase();
    test_stress();
    test_good_files();
    test_top_level();
    test_nodes();
    test_cameras_and_lights();
    test_lighting();
    test_models();
    test_behaviors();
    test_benchmark();
    if (!failures)
        printf("scene_file_test: ok\n");
    return failures ? 1 : 0;
}
