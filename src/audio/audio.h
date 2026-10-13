// FIGHTO - procedural audio: synthesized sound effects, 3D mixer and adaptive music.
#pragma once
#include "../core/vmath.h"

enum SoundId {
    SND_PUNCH, SND_PUNCH_HEAVY, SND_KICK, SND_WHOOSH, SND_BLOCK, SND_BONE_CRACK, SND_SPLAT, SND_BODYFALL,
    SND_FOOTSTEP, SND_FOOTSTEP_WET, SND_PISTOL, SND_SHOTGUN, SND_SMG, SND_RIFLE, SND_MAGNUM, SND_ARC, SND_LANCE,
    SND_SHARD, SND_RELOAD, SND_EMPTY, SND_SHELL, SND_RICOCHET, SND_IMPACT_FLESH, SND_IMPACT_METAL, SND_IMPACT_CONCRETE,
    SND_GLASS, SND_EXPLOSION, SND_CAR_DOOR, SND_CRASH, SND_HORN, SND_SKID, SND_ALIEN_SCREECH, SND_ALIEN_GROWL,
    SND_ALIEN_HIT, SND_DRONE_SHOT, SND_UI_CLICK, SND_UI_HOVER, SND_UI_BACK, SND_MISSION_START, SND_MISSION_COMPLETE,
    SND_MISSION_FAIL, SND_NOTIFY, SND_PICKUP, SND_MONEY, SND_THUNDER, SND_HEARTBEAT, SND_GRUNT, SND_SCREAM,
    SND_DIALOGUE_BLIP, SND_CHECKPOINT, SND_ELEVATOR, SND_SHARD_PULSE, SND_CROWD_CHEER, SND_SPLASH_BLOOD,
    SND_COUNT
};

enum MusicMood { MUSIC_NONE, MUSIC_MENU, MUSIC_CALM, MUSIC_TENSION, MUSIC_COMBAT, MUSIC_ALIEN, MUSIC_CHASE };

bool audio_init();
void audio_shutdown();
void audio_play(int snd, float vol = 1.0f, float pitch = 1.0f);
void audio_play_at(int snd, vec3 pos, float vol = 1.0f, float pitch = 1.0f);
void audio_set_listener(vec3 pos, vec3 forward);
void audio_set_volumes(float master, float music, float sfx, float voice);
void audio_engine(int slot, vec3 pos, float rpm01, float load, bool active);
void audio_siren(vec3 pos, float vol);
void audio_set_ambience(float rain, float city, float indoor, float invasion);
void audio_set_music(int mood, float intensity);
void audio_pause(bool paused);
void audio_frame();   // game thread, once per frame
int audio_active_voices();
// Renders `seconds` of the full mix offline (used by tests to validate the synth).
float audio_debug_render_peak(int snd);
