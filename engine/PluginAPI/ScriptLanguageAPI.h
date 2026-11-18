#pragma once

/*
 * C ABI for scripting-language plugins.
 *
 * Plain C on purpose: a language plugin can be written in C, C++, Rust or Zig,
 * or wrap an embedded runtime (Python, JavaScript, .NET, ...). Nothing here
 * depends on Qt, the STL, or the editor, so the same module runs in the editor
 * and in the packaged game.
 *
 * A language module is a shared library named by the plugin manifest's
 * "runtimeEntry" field. It exports HP_GetScriptLanguages, which returns the
 * languages it provides. Lua is built into the engine and does not use this API.
 *
 * Every call happens on the gameplay thread. Strings passed to the plugin are
 * borrowed for the duration of the call only.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define HP_SCRIPT_LANGUAGE_EXPORT __declspec(dllexport)
#else
#  define HP_SCRIPT_LANGUAGE_EXPORT __attribute__((visibility("default")))
#endif

#define HP_SCRIPT_LANGUAGE_API_VERSION 1u
#define HP_SCRIPT_LANGUAGES_EXPORT_NAME "HP_GetScriptLanguages"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene entity handle. 0 means "no entity". */
typedef uint64_t HPEntityId;

typedef struct HPVec3 {
    float x;
    float y;
    float z;
} HPVec3;

typedef struct HPVec2 {
    float x;
    float y;
} HPVec2;

typedef enum HPValueType {
    HP_VALUE_NIL = 0,
    HP_VALUE_BOOL = 1,
    HP_VALUE_NUMBER = 2,
    HP_VALUE_STRING = 3,
    HP_VALUE_VEC2 = 4,
    HP_VALUE_VEC3 = 5,
    HP_VALUE_VEC4 = 6
} HPValueType;

/* A script value crossing the language boundary. Only the member matching
   `type` is meaningful. VEC2/VEC3/VEC4 use the first 2/3/4 entries of `vector`. */
typedef struct HPValue {
    uint32_t type;
    int32_t boolean;
    double number;
    const char* string;
    float vector[4];
} HPValue;

typedef struct HPNamedValue {
    const char* name;
    HPValue value;
} HPNamedValue;

/* Result of HPScriptHostAPI.raycast. */
typedef struct HPRaycastHit {
    HPVec3 point;
    HPVec3 normal;
    float distance;
    HPEntityId entity; /* 0 when the hit has no entity. */
} HPRaycastHit;

/* Lens of a Camera component. Field of view in degrees. */
typedef struct HPCameraLens {
    float fieldOfView;
    float nearPlane;
    float farPlane;
} HPCameraLens;

typedef enum HPCursorMode {
    HP_CURSOR_NORMAL = 0,  /* Visible and free, for menus. */
    HP_CURSOR_CAPTURED = 1 /* Hidden and locked, for mouse look. */
} HPCursorMode;

typedef enum HPComponentKind {
    HP_COMPONENT_CAMERA = 0,
    HP_COMPONENT_CHARACTER = 1, /* Animated character: animation and sockets. */
    HP_COMPONENT_MESH = 2,
    HP_COMPONENT_CHARACTER_CONTROLLER = 3
} HPComponentKind;

typedef enum HPUIWidgetKind {
    HP_UI_PANEL = 0,
    HP_UI_TEXT = 1,
    HP_UI_BUTTON = 2,
    HP_UI_IMAGE = 3,
    HP_UI_VERTICAL_BOX = 4,
    HP_UI_HORIZONTAL_BOX = 5,
    HP_UI_GRID = 6
} HPUIWidgetKind;

/* What HPScriptHostAPI.uiSet changes, one per Lua UI.set* function, with the
   value type it takes. */
typedef enum HPUIProperty {
    HP_UI_POSITION = 0,          /* VEC2 */
    HP_UI_SIZE = 1,              /* VEC2 */
    HP_UI_VISIBLE = 2,           /* BOOL */
    HP_UI_OPACITY = 3,           /* NUMBER */
    HP_UI_Z_ORDER = 4,           /* NUMBER */
    HP_UI_TEXT_CONTENT = 5,      /* STRING, a Text's text or a Button's label */
    HP_UI_IMAGE_ID = 6,          /* STRING */
    HP_UI_SPACING = 7,           /* NUMBER, boxes */
    HP_UI_GRID_COLUMNS = 8,      /* NUMBER */
    HP_UI_GRID_SPACING = 9,      /* VEC2, horizontal and vertical */
    HP_UI_ENABLED = 10,          /* BOOL */
    HP_UI_PADDING = 11,          /* NUMBER for every side, or VEC4 top, right, bottom, left */
    HP_UI_MARGIN = 12,           /* as HP_UI_PADDING */
    HP_UI_MIN_SIZE = 13,         /* VEC2 */
    HP_UI_MAX_SIZE = 14,         /* VEC2 */
    HP_UI_ANCHOR = 15,           /* STRING: topLeft, topCenter, ..., bottomRight, stretch */
    HP_UI_H_ALIGNMENT = 16,      /* STRING: left, center, right, stretch */
    HP_UI_V_ALIGNMENT = 17,      /* STRING: top, center, bottom, stretch */
    HP_UI_BACKGROUND_COLOR = 18, /* VEC4 r, g, b, a */
    HP_UI_BORDER_COLOR = 19,     /* VEC4 */
    HP_UI_BORDER_WIDTH = 20,     /* NUMBER */
    HP_UI_CORNER_RADIUS = 21,    /* NUMBER */
    HP_UI_TEXT_COLOR = 22,       /* VEC4 */
    HP_UI_FONT_SIZE = 23,        /* NUMBER */
    HP_UI_FONT_BOLD = 24,        /* BOOL */
    HP_UI_FONT_ITALIC = 25,      /* BOOL */
    HP_UI_TEXT_ALIGNMENT = 26,   /* STRING: left, center, right */
    HP_UI_TEXT_SHADOW = 27,      /* BOOL */
    HP_UI_TEXT_SHADOW_COLOR = 28,  /* VEC4 */
    HP_UI_TEXT_SHADOW_OFFSET = 29, /* VEC2 */
    HP_UI_WORD_WRAP = 30,        /* BOOL */
    HP_UI_FOCUSED = 31,          /* BOOL, buttons */
    HP_UI_CHILD_ALIGNMENT = 32,  /* STRING, boxes: left, center, right / top, center, bottom */
    HP_UI_IMAGE_SCALE_MODE = 33, /* STRING: stretch, fit, fill, center */
    HP_UI_IMAGE_TINT = 34        /* VEC4 */
} HPUIProperty;

typedef enum HPLogLevel {
    HP_LOG_INFO = 0,
    HP_LOG_WARNING = 1,
    HP_LOG_ERROR = 2
} HPLogLevel;

/*
 * Engine services a language runtime can call. Functions returning int32_t
 * return 1 on success and 0 when the entity or value does not exist.
 * Rotations are Euler angles in radians, matching TransformComponent.
 *
 * New members are only ever appended. Check structSize before using a member
 * added after the version your plugin was built against.
 */
typedef struct HPScriptHostAPI {
    uint32_t structSize;
    uint32_t apiVersion;
    void* host;

    void (*log)(void* host, uint32_t level, const char* message);

    int32_t (*isEntityValid)(void* host, HPEntityId entity);
    HPEntityId (*findEntity)(void* host, const char* name);
    /* Copies the name into buffer when it fits and returns its length
       including the terminator, so a call with bufferSize 0 sizes the buffer. */
    size_t (*getEntityName)(void* host, HPEntityId entity, char* buffer, size_t bufferSize);

    int32_t (*getPosition)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setPosition)(void* host, HPEntityId entity, HPVec3 value);
    int32_t (*getRotation)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setRotation)(void* host, HPEntityId entity, HPVec3 value);
    int32_t (*getScale)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setScale)(void* host, HPEntityId entity, HPVec3 value);

    /* Named actions and axes from the project's .hpinput profile. */
    int32_t (*isActionDown)(void* host, const char* action);
    int32_t (*isActionPressed)(void* host, const char* action);
    int32_t (*isActionReleased)(void* host, const char* action);
    float (*getAxis)(void* host, const char* axis);

    /* Calls a function on the script attached to an entity, whatever language
       it is written in (Lua included). Returns 1 when a script handled it. */
    int32_t (*callEntityFunction)(void* host, HPEntityId entity, const char* function,
                                  const HPValue* args, uint32_t argCount);

    /* Added after the first release: check structSize before use.
       The project folder (the game's content folder when packaged), copied
       with the getEntityName buffer convention. */
    size_t (*getProjectRoot)(void* host, char* buffer, size_t bufferSize);

    /* Everything below was added together, after getProjectRoot: check
       structSize covers the member you use. They mirror the Lua API of the
       same name (see docs/user-guide/lua-api.md); differences are noted. */

    /* Time and application */
    float (*getDeltaTime)(void* host);
    void (*quit)(void* host); /* Closes the game; stops Play in the editor. */

    /* Entities and scene. findEntitiesWithPrefix and findCameras write up to
       `capacity` ids and return how many exist, so a call with capacity 0
       sizes the array. */
    int32_t (*setEntityName)(void* host, HPEntityId entity, const char* name);
    int32_t (*hasComponent)(void* host, HPEntityId entity, uint32_t kind); /* HPComponentKind */
    uint32_t (*findEntitiesWithPrefix)(void* host, const char* prefix, HPEntityId* out, uint32_t capacity);
    /* position and rotationDegrees may be NULL to keep the prefab's own. vars
       override the prefab scripts' Inspector values. Returns 0 on failure. */
    HPEntityId (*instantiatePrefab)(void* host, const char* path, const HPVec3* position,
                                    const HPVec3* rotationDegrees, const HPNamedValue* vars, uint32_t varCount);
    /* Removes the entity (a prefab instance as a whole) once this frame's scripts have run. */
    int32_t (*destroyEntity)(void* host, HPEntityId entity);
    int32_t (*reloadScene)(void* host);
    int32_t (*setMeshVisible)(void* host, HPEntityId entity, int32_t visible);
    int32_t (*isMeshVisible)(void* host, HPEntityId entity);

    /* Raw input. Keys use the names Lua's Input.isKeyPressed takes ("w",
       "space", "left_shift", ...); mouse buttons are 0 left, 1 right, 2 middle. */
    int32_t (*isKeyDown)(void* host, const char* key);
    int32_t (*isMouseButtonDown)(void* host, int32_t button);
    HPVec2 (*getMousePosition)(void* host);
    HPVec2 (*getMouseDelta)(void* host);
    HPVec2 (*getScrollDelta)(void* host);
    int32_t (*isGamepadConnected)(void* host);
    void (*setCursorMode)(void* host, uint32_t mode); /* HPCursorMode */
    uint32_t (*getCursorMode)(void* host);

    /* The script view camera (Lua's Camera table). Rotation is pitch, yaw,
       roll in radians. */
    void (*viewCameraSetActive)(void* host, int32_t active);
    int32_t (*viewCameraIsActive)(void* host);
    void (*viewCameraSetPosition)(void* host, HPVec3 position);
    HPVec3 (*viewCameraGetPosition)(void* host);
    void (*viewCameraSetRotation)(void* host, HPVec3 radians);
    HPVec3 (*viewCameraGetRotation)(void* host);
    void (*viewCameraTranslate)(void* host, HPVec3 delta);

    /* Camera components. Making one active makes every other inactive. */
    uint32_t (*findCameras)(void* host, HPEntityId* out, uint32_t capacity);
    HPEntityId (*getActiveCamera)(void* host); /* The active camera, else the first, else 0. */
    int32_t (*setCameraActive)(void* host, HPEntityId camera, int32_t active);
    int32_t (*isCameraActive)(void* host, HPEntityId camera);
    int32_t (*getCameraLens)(void* host, HPEntityId camera, HPCameraLens* out);
    int32_t (*setCameraLens)(void* host, HPEntityId camera, const HPCameraLens* lens);

    /* Physics bodies. `activate` wakes a sleeping body (Lua's default is 1).
       raycast ignores `ignore` (pass 0 for none) and returns 1 on a hit; a bad
       layer name returns 0 with a message in `error`. */
    int32_t (*isPhysicsAvailable)(void* host);
    int32_t (*hasBody)(void* host, HPEntityId entity);
    int32_t (*getLinearVelocity)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setLinearVelocity)(void* host, HPEntityId entity, HPVec3 velocity, int32_t activate);
    int32_t (*setAngularVelocity)(void* host, HPEntityId entity, HPVec3 velocity, int32_t activate);
    int32_t (*setBodyRotation)(void* host, HPEntityId entity, HPVec3 radians, int32_t activate);
    int32_t (*raycast)(void* host, HPEntityId ignore, HPVec3 origin, HPVec3 direction, float maxDistance,
                       const char* layer, int32_t ignoreSensors, HPRaycastHit* out, char* error, size_t errorSize);
    int32_t (*physicsIsGrounded)(void* host, HPEntityId entity);
    int32_t (*physicsMove)(void* host, HPEntityId entity, float x, float z, float speed);
    int32_t (*physicsJump)(void* host, HPEntityId entity, float speed);

    /* Character controllers. getGroundState copies with the getEntityName convention. */
    int32_t (*characterMove)(void* host, HPEntityId entity, float worldX, float worldZ);
    int32_t (*characterJump)(void* host, HPEntityId entity);
    int32_t (*characterSetCrouched)(void* host, HPEntityId entity, int32_t crouched);
    int32_t (*characterIsCrouched)(void* host, HPEntityId entity);
    int32_t (*characterIsStandBlocked)(void* host, HPEntityId entity);
    int32_t (*characterIsGrounded)(void* host, HPEntityId entity);
    size_t (*characterGetGroundState)(void* host, HPEntityId entity, char* buffer, size_t bufferSize);
    int32_t (*characterGetVelocity)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*characterGetGroundNormal)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*characterGetGroundVelocity)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*characterSetSpeedMultiplier)(void* host, HPEntityId entity, float multiplier);
    float (*characterGetSpeedMultiplier)(void* host, HPEntityId entity);
    int32_t (*characterTeleport)(void* host, HPEntityId entity, HPVec3 position);
    int32_t (*characterSetFacingYaw)(void* host, HPEntityId entity, float radians);

    /* Animated characters. A NULL clip means "whatever is playing". Sockets
       are in world space, or character space when worldSpace is 0; rotation
       is Euler radians. */
    int32_t (*animationPlay)(void* host, HPEntityId entity, const char* clip);
    int32_t (*animationCrossFade)(void* host, HPEntityId entity, const char* clip, float seconds);
    int32_t (*animationPause)(void* host, HPEntityId entity);
    int32_t (*animationResume)(void* host, HPEntityId entity);
    int32_t (*animationStop)(void* host, HPEntityId entity);
    int32_t (*animationSetSpeed)(void* host, HPEntityId entity, float speed);
    int32_t (*animationIsPlaying)(void* host, HPEntityId entity, const char* clip);
    size_t (*animationGetCurrent)(void* host, HPEntityId entity, char* buffer, size_t bufferSize);
    float (*animationGetTime)(void* host, HPEntityId entity);
    float (*animationGetDuration)(void* host, HPEntityId entity, const char* clip);
    int32_t (*getSocket)(void* host, HPEntityId entity, const char* socket, int32_t worldSpace,
                         HPVec3* position, HPVec3* rotation);

    /* Audio. Handles are 0 when a sound failed to start. */
    int32_t (*isAudioAvailable)(void* host);
    uint32_t (*audioPlay)(void* host, const char* path, int32_t loop, float volume);
    int32_t (*audioStop)(void* host, uint32_t sound);
    int32_t (*audioPause)(void* host, uint32_t sound);
    int32_t (*audioResume)(void* host, uint32_t sound);
    int32_t (*audioSetVolume)(void* host, uint32_t sound, float volume);
    int32_t (*audioIsPlaying)(void* host, uint32_t sound);
    void (*audioStopAll)(void* host);

    /* Game UI (Lua's UI, display and destroy tables). Screens and widgets are
       handles shared with Lua, so either language can change a widget the
       other made; 0 means none. uiDisplay and uiLoad write a message into
       `error` when they return 0. uiDestroy with screen 0 destroys the most
       recently displayed one. Style fields use the names of Lua's style
       tables (backgroundColor, fontSize, padding, textShadowOffset, ...),
       with colours as VEC4 and enums as strings; buttonState is NULL for
       UI.setStyle, or normal, hovered, pressed, focused or disabled. */
    int32_t (*uiIsAvailable)(void* host);
    uint32_t (*uiDisplay)(void* host, const char* path, char* error, size_t errorSize);
    int32_t (*uiDestroy)(void* host, uint32_t screen);
    void (*uiDestroyAll)(void* host);
    uint32_t (*uiLoad)(void* host, const char* path, char* error, size_t errorSize);
    uint32_t (*uiGetWidget)(void* host, uint32_t screen, const char* id);
    uint32_t (*uiCreateScreen)(void* host, const char* name);
    uint32_t (*uiCreateSimpleMenu)(void* host);
    uint32_t (*uiGetRoot)(void* host, uint32_t screen);
    /* text is a Text's text, a Button's label or an Image's image id. */
    uint32_t (*uiCreateWidget)(void* host, uint32_t parent, uint32_t kind, const char* text); /* HPUIWidgetKind */
    void (*uiClearScreens)(void* host);
    uint32_t (*uiGetScreenCount)(void* host);
    int32_t (*uiSetScreenVisible)(void* host, const char* name, int32_t visible);
    int32_t (*uiSetScreenActive)(void* host, const char* name, int32_t active);
    int32_t (*uiSetViewportSize)(void* host, float width, float height);
    uint32_t (*uiGetDrawCommandCount)(void* host);
    int32_t (*uiSet)(void* host, uint32_t widget, uint32_t property, const HPValue* value); /* HPUIProperty */
    int32_t (*uiSetStyle)(void* host, uint32_t widget, const char* buttonState, const HPNamedValue* fields,
                          uint32_t fieldCount);
    int32_t (*uiRegisterThemeStyle)(void* host, const char* name, const HPNamedValue* fields, uint32_t fieldCount);
    int32_t (*uiApplyThemeStyle)(void* host, uint32_t widget, const char* name);
    /* HP_CURSOR_NORMAL frees the mouse for menus (Lua's CursorMode.UI);
       HP_CURSOR_CAPTURED gives it back to the game (CursorMode.Game). */
    int32_t (*uiSetCursorMode)(void* host, uint32_t mode);
    uint32_t (*uiGetCursorMode)(void* host);
    /* Clicking the button runs `function` on the entity's script, in whatever
       language it is written; hovering runs it with one BOOL, true on enter. */
    int32_t (*uiSetOnClick)(void* host, uint32_t widget, HPEntityId entity, const char* function);
    int32_t (*uiSetOnHover)(void* host, uint32_t widget, HPEntityId entity, const char* function);

    /* Like callEntityFunction, but reaches the entity's character controller
       script (Lua's entity:CallController). */
    int32_t (*callControllerFunction)(void* host, HPEntityId entity, const char* function,
                                      const HPValue* args, uint32_t argCount);
} HPScriptHostAPI;

/* Describes the script a runtime is asked to load for one entity: its Script
   component's, or the character controller script of its Character
   component, which runs the same way. */
typedef struct HPScriptInstanceInfo {
    HPEntityId entity;
    const char* scriptPath;   /* As authored, e.g. "assets/scripts/player.py". */
    const char* resolvedPath; /* Absolute path of the file on disk. */
    const HPNamedValue* variables; /* Per-entity values set in the inspector. */
    uint32_t variableCount;
} HPScriptInstanceInfo;

typedef enum HPContactKind {
    HP_CONTACT_TRIGGER_ENTER = 0,
    HP_CONTACT_TRIGGER_STAY = 1,
    HP_CONTACT_TRIGGER_EXIT = 2,
    HP_CONTACT_COLLISION_ENTER = 3,
    HP_CONTACT_COLLISION_STAY = 4,
    HP_CONTACT_COLLISION_EXIT = 5
} HPContactKind;

typedef struct HPContactEvent {
    uint32_t kind;     /* HPContactKind */
    HPEntityId other;  /* 0 when the other entity no longer exists. */
    int32_t hasContact; /* When 0 the contact members below are unset. */
    HPVec3 point;
    HPVec3 normal;
    HPVec3 relativeVelocity;
    float penetration;
} HPContactEvent;

typedef void (*HPFieldCallback)(void* user, const char* name, HPValue defaultValue);

/* Colour roles for editor syntax highlighting; the editor theme picks the colours. */
typedef enum HPSyntaxStyle {
    HP_SYNTAX_KEYWORD = 0,
    HP_SYNTAX_TYPE = 1,
    HP_SYNTAX_FUNCTION = 2,
    HP_SYNTAX_STRING = 3,
    HP_SYNTAX_NUMBER = 4,
    HP_SYNTAX_COMMENT = 5
} HPSyntaxStyle;

/* Patterns are Perl-compatible regular expressions (PCRE2) matched within one
   line. The editor scans each line left to right: the earliest match wins, a
   tie goes to the rule listed first, and scanning resumes after the match. */
typedef struct HPSyntaxRule {
    uint32_t style; /* HPSyntaxStyle */
    const char* pattern;
} HPSyntaxRule;

/* A span that can cross lines, such as a block comment or long string. */
typedef struct HPSyntaxBlock {
    uint32_t style; /* HPSyntaxStyle */
    const char* startPattern;
    const char* endPattern;
} HPSyntaxBlock;

typedef enum HPCompletionKind {
    HP_COMPLETION_KEYWORD = 0,
    HP_COMPLETION_FUNCTION = 1,
    HP_COMPLETION_VARIABLE = 2,
    HP_COMPLETION_FIELD = 3,
    HP_COMPLETION_TYPE = 4
} HPCompletionKind;

/* One suggestion. `detail` (may be NULL) is shown beside the label. */
typedef void (*HPCompletionCallback)(void* user, const char* label, uint32_t kind, const char* detail);

typedef struct HPSyntax {
    const HPSyntaxRule* rules;
    uint32_t ruleCount;
    const HPSyntaxBlock* blocks;
    uint32_t blockCount;
    /* Inserted by the editor's Toggle Comment (Ctrl+/), e.g. "#". NULL disables it. */
    const char* lineComment;
} HPSyntax;

/*
 * One scripting language. Required members: id, displayName, extensions,
 * createRuntime, destroyRuntime, createInstance and destroyInstance. Every
 * other callback may be NULL.
 */
typedef struct HPScriptLanguage {
    uint32_t structSize;
    uint32_t apiVersion;
    const char* id;          /* Stable lowercase id, e.g. "python". */
    const char* displayName; /* e.g. "Python". */
    const char* extensions;  /* Semicolon-separated, e.g. ".py;.pyw". */

    /* Called when play starts and the first script in this language needs to
       run. `host` stays valid until destroyRuntime. */
    void* (*createRuntime)(const HPScriptHostAPI* host);
    void (*destroyRuntime)(void* runtime);

    /* Loads a script for one entity. Return NULL and write a message into
       `error` on failure. Called again after the file changes on disk. */
    void* (*createInstance)(void* runtime, const HPScriptInstanceInfo* info,
                            char* error, size_t errorSize);
    /* Run the script's own teardown (for example onDestroy) here. */
    void (*destroyInstance)(void* runtime, void* instance);

    void (*awake)(void* runtime, void* instance);
    void (*update)(void* runtime, void* instance, float deltaSeconds);
    void (*lateUpdate)(void* runtime, void* instance, float deltaSeconds);
    void (*contact)(void* runtime, void* instance, const HPContactEvent* event);
    /* Returns 1 when the script defines `function` and it ran. */
    int32_t (*call)(void* runtime, void* instance, const char* function,
                    const HPValue* args, uint32_t argCount);

    /* Lists the inspector-editable fields of a script file and their default
       values, without a runtime. Return 0 when the file cannot be read. */
    int32_t (*describeFields)(const char* resolvedPath, HPFieldCallback callback, void* user);

    /* Added after the first release: read only when structSize covers it.
       Highlighting for the editor's code editor; NULL shows plain text. */
    const HPSyntax* syntax;

    /* Editor language services, read only when structSize covers them. Both
       get the whole unsaved file in `source`; `line` and `column` are 1-based,
       column counted in bytes of that line's UTF-8 text. */

    /* Suggestions for the cursor position. The editor filters them by the
       word being typed, so return every candidate. Return 0 for none. */
    int32_t (*complete)(const char* path, const char* source, uint32_t line, uint32_t column,
                        HPCompletionCallback callback, void* user);
    /* Plain-text documentation for the symbol under the position, written into
       `buffer`. Returns the length including the terminator (0 for nothing),
       so a call with bufferSize 0 sizes the buffer. */
    size_t (*hover)(const char* path, const char* source, uint32_t line, uint32_t column,
                    char* buffer, size_t bufferSize);

    /* Packaging, read only when structSize covers it. Called once for every
       file of this language a package contains (project scripts and enabled
       plugins' content), with absolute paths, before the package is
       committed. scriptPath is the staged copy inside packageRoot, which
       becomes the game's project root, so prepare that copy; projectRoot is
       the project being packaged, for caches. Write whatever the script needs
       at runtime (for example compiled code) under packageRoot. Return 0 and
       write a message into `error` to fail the build. */
    int32_t (*packageScript)(const char* projectRoot, const char* scriptPath, const char* packageRoot,
                             char* error, size_t errorSize);

    /* Contents of a new script made from the Content Browser, read only when
       structSize covers it; NULL leaves the language out of the menu. Every
       "$NAME" becomes the name the user typed, which is a valid identifier. */
    const char* newScriptTemplate;

    /* Read only when structSize covers it. A character's non-looping clip
       finished this frame (Lua's onAnimationFinished); `clip` is its id. */
    void (*animationFinished)(void* runtime, void* instance, const char* clip);
} HPScriptLanguage;

/* Signature of the HP_GetScriptLanguages export. Return NULL when
   hostApiVersion is not supported. */
typedef const HPScriptLanguage* const* (*HPGetScriptLanguagesFn)(uint32_t hostApiVersion,
                                                                 uint32_t* count);

#ifdef __cplusplus
}
#endif
