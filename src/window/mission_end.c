#include "mission_end.h"

#include "assets/assets.h"
#include "campaign/campaign.h"
#include "city/emperor.h"
#include "city/finance.h"
#include "city/population.h"
#include "city/ratings.h"
#include "city/victory.h"
#include "core/encoding.h"
#include "core/image_group.h"
#include "game/mission.h"
#include "game/settings.h"
#include "game/state.h"
#include "game/undo.h"
#include "graphics/generic_button.h"
#include "graphics/graphics.h"
#include "graphics/image.h"
#include "graphics/lang_text.h"
#include "graphics/panel.h"
#include "graphics/rich_text.h"
#include "graphics/text.h"
#include "graphics/window.h"
#include "input/input.h"
#include "scenario/custom_messages.h"
#include "scenario/property.h"
#include "scenario/scenario.h"
#include "sound/channel.h"
#include "sound/device.h"
#include "sound/music.h"
#include "sound/speech.h"
#include "window/intermezzo.h"
#include "window/main_menu.h"
#include "window/mission_selection.h"
#include "window/video.h"

#include <string.h>

static void button_fired(int param1, int param2);

static generic_button fired_buttons[] = {
    {80, 224, 480, 25, button_fired, button_none, 0, 0},
};

static struct {
    int focus_button_id;
    int audio_playing;
    struct {
        char audio[FILE_NAME_MAX];
        char speech[FILE_NAME_MAX];
        char background_music[FILE_NAME_MAX];
    } paths;
} data;

static void draw_lost(void)
{
    window_draw_underlying_window();
    graphics_in_dialog();

    outer_panel_draw(48, 16, 34, 16);
    lang_text_draw_centered(62, 1, 48, 32, 544, FONT_LARGE_BLACK);
    lang_text_draw_multiline(62, 16, 64, 72, 496, FONT_NORMAL_BLACK);

    graphics_reset_dialog();
}

static int get_max(int value1, int value2, int value3)
{
    int max = value1;
    if (value2 > max) {
        max = value2;
    }
    if (value3 > max) {
        max = value3;
    }
    return max;
}

static int has_custom_victory_message(void)
{
    return scenario_victory_message() && custom_messages_get(scenario_victory_message());
}

static void fadeout_music(int unused)
{
    sound_device_fadeout_music(5000);
    sound_device_on_audio_finished(0);
}

static void init_speech(int channel)
{
    if (channel != SOUND_CHANNEL_SPEECH) {
        return;
    }

    int has_speech = *data.paths.speech && *data.paths.background_music;
    if (*data.paths.speech) {
        has_speech &= sound_device_play_file_on_channel(data.paths.speech,
            SOUND_CHANNEL_SPEECH, setting_sound(SOUND_SPEECH)->volume);
    }
    if (*data.paths.background_music) {
        int volume = 100;
        if (has_speech) {
            volume = setting_sound(SOUND_SPEECH)->volume / 3;
        }
        if (volume > setting_sound(SOUND_MUSIC)->volume) {
            volume = setting_sound(SOUND_MUSIC)->volume;
        }
        has_speech &= sound_device_play_music(data.paths.background_music, volume, 0);
    }
    sound_device_on_audio_finished(has_speech ? fadeout_music : 0);
}

static void play_audio(void)
{
    if (data.audio_playing || !has_custom_victory_message()) {
        return;
    }

    data.audio_playing = 1;
    data.paths.audio[0] = 0;
    data.paths.speech[0] = 0;
    data.paths.background_music[0] = 0;   

    custom_message_t *custom_message = custom_messages_get(scenario_intro_message());

    const char *audio_file = custom_messages_get_audio(custom_message);
    if (audio_file) {
        strncpy(data.paths.audio, audio_file, FILE_NAME_MAX);
    }
    const char *speech_file = custom_messages_get_speech(custom_message);
    if (speech_file) {
        strncpy(data.paths.speech, speech_file, FILE_NAME_MAX);
    }
    const char *background_music = custom_messages_get_background_music(custom_message);
    if (background_music) {
        strncpy(data.paths.background_music, background_music, FILE_NAME_MAX);
    }
    int playing_audio = 0;

    if (audio_file) {
        playing_audio = sound_device_play_file_on_channel(data.paths.audio, SOUND_CHANNEL_SPEECH,
            setting_sound(SOUND_SPEECH)->volume);
    }
    if (speech_file) {
        if (!playing_audio) {
            init_speech(SOUND_CHANNEL_SPEECH);
        } else {
            sound_device_on_audio_finished(init_speech);
        }
    } else if (background_music) {
        sound_device_play_music(data.paths.background_music, setting_sound(SOUND_MUSIC)->volume, 0);
    }
}

static void draw_background_image(void)
{
    if (!has_custom_victory_message()) {
        window_draw_underlying_window();
        return;
    }
    graphics_clear_screen();

    const campaign_scenario *scenario = campaign_get_scenario(scenario_campaign_mission());
    int image_id = 0;
    if (scenario->victory_image_path) {
        image_id = assets_get_external_image(scenario->victory_image_path, 0);
    } else {
        image_id = image_group(GROUP_INTERMEZZO_BACKGROUND) + 2 * (scenario_campaign_mission() % 11) + 2;
    }
    image_draw_fullscreen_background(image_id);
}

static void draw_won(void)
{
    int panel_height_blocks;
    int y_offset;
    const uint8_t *victory_message_text = 0;
    if (has_custom_victory_message()) {
        custom_message_t *victory_message = custom_messages_get(scenario_victory_message());
        victory_message_text = custom_messages_get_text(victory_message);
        if (victory_message_text) {
            y_offset = 0;
            panel_height_blocks = 30;
        }
    } else {
        y_offset = 128;
        panel_height_blocks = 18;
    }

    play_audio();
    draw_background_image();

    int info_y_offset = panel_height_blocks * BLOCK_SIZE + y_offset - 108;

    graphics_in_dialog();

    outer_panel_draw(48, y_offset, 34, panel_height_blocks);
    lang_text_draw_centered(62, 0, 48, y_offset + 16, 544, FONT_LARGE_BLACK);

    if (victory_message_text) {
        rich_text_set_fonts(FONT_NORMAL_WHITE, FONT_NORMAL_WHITE, 5);
        int width_blocks = rich_text_init(victory_message_text, 96, y_offset + 56, 28, panel_height_blocks - 11, 1);
        inner_panel_draw(64, y_offset + 56, width_blocks + 2, panel_height_blocks - 11);
        rich_text_draw(victory_message_text, 80, y_offset + 64, width_blocks * BLOCK_SIZE, panel_height_blocks - 12, 0);
    } else if (scenario_is_custom()) {
        inner_panel_draw(64, y_offset + 56, 32, panel_height_blocks - 11);
        lang_text_draw_multiline(147, 20, 80, y_offset + 64, 488, FONT_NORMAL_WHITE);
    } else {
        inner_panel_draw(64, y_offset + 56, 32, panel_height_blocks - 11);
        lang_text_draw_multiline(147, scenario_campaign_mission(), 80, y_offset + 64, 488, FONT_NORMAL_WHITE);
    }

    int left_width = get_max(
        lang_text_get_width(148, 0, FONT_NORMAL_BLACK),
        lang_text_get_width(148, 2, FONT_NORMAL_BLACK),
        lang_text_get_width(148, 4, FONT_NORMAL_BLACK)
    );
    int right_width = get_max(
        lang_text_get_width(148, 1, FONT_NORMAL_BLACK),
        lang_text_get_width(148, 3, FONT_NORMAL_BLACK),
        lang_text_get_width(148, 5, FONT_NORMAL_BLACK)
    );
    int left_offset = 68;
    int right_offset = left_offset + 10 + 512 * left_width / (left_width + right_width);
    int width = lang_text_draw(148, 0, left_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_rating_culture(), '@', " ", left_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    width = lang_text_draw(148, 1, right_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_rating_prosperity(), '@', " ", right_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    info_y_offset += 20;
    
    width = lang_text_draw(148, 2, left_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_rating_peace(), '@', " ", left_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    width = lang_text_draw(148, 3, right_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_rating_favor(), '@', " ", right_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    info_y_offset += 20;

    width = lang_text_draw(148, 4, left_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_population(), '@', " ", left_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    width = lang_text_draw(148, 5, right_offset, info_y_offset, FONT_NORMAL_BLACK);
    text_draw_number(city_finance_treasury(), '@', " ", right_offset + width, info_y_offset, FONT_NORMAL_BLACK, 0);

    info_y_offset += 40;

    lang_text_draw_centered(13, 1, 64, info_y_offset, 512, FONT_NORMAL_BLACK);

    graphics_reset_dialog();
}

static void draw_background(void)
{
    if (city_victory_state() == VICTORY_STATE_WON) {
        draw_won();
    } else {
        draw_lost();
    }
}

static void draw_foreground(void)
{
    graphics_in_dialog();

    if (city_victory_state() != VICTORY_STATE_WON) {
        large_label_draw(80, 224, 30, data.focus_button_id == 1);
        lang_text_draw_centered(62, 6, 80, 230, 480, FONT_NORMAL_GREEN);
    } else if (has_custom_victory_message()) {
        rich_text_draw_scrollbar();
    }

    graphics_reset_dialog();
}

static void advance_to_next_mission(void)
{
    setting_set_personal_savings_for_mission(scenario_campaign_rank() + 1, city_emperor_personal_savings());
    scenario_set_campaign_rank(scenario_campaign_rank() + 1);
    scenario_save_campaign_player_name();

    city_victory_stop_governing();

    game_undo_disable();
    game_state_reset_overlay();

    const campaign_mission_info *mission_info = campaign_get_next_mission(scenario_campaign_mission());

    if (mission_info) {
        scenario_set_campaign_mission(mission_info->first_scenario);
        window_mission_selection_show();
    } else if (scenario_campaign_rank() >= 11 || scenario_is_custom()) {
        window_main_menu_show(1);
        setting_clear_personal_savings();
        scenario_settings_init();
        scenario_set_campaign_rank(2);
    } else {
        scenario_set_campaign_mission(game_mission_peaceful());
        window_mission_selection_show();
    }
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    if (city_victory_state() == VICTORY_STATE_WON) {
        if (input_go_back_requested(m, h)) {
            sound_music_stop();
            sound_speech_stop();
            advance_to_next_mission();
        } else if (has_custom_victory_message()) {
            rich_text_handle_mouse(mouse_in_dialog(m));
        }
    } else {
        generic_buttons_handle_mouse(mouse_in_dialog(m), 0, 0, fired_buttons, 1, &data.focus_button_id);
    }
}

static void button_fired(int param1, int param2)
{
    sound_music_stop();
    sound_speech_stop();
    city_victory_stop_governing();
    game_undo_disable();
    if (scenario_is_custom() && !campaign_is_active()) {
        window_main_menu_show(1);
    } else {
        window_mission_selection_show_again();
    }
}

static void show_end_dialog(void)
{
    data.audio_playing = 0;
    window_type window = {
        WINDOW_MISSION_END,
        draw_background,
        draw_foreground,
        handle_input
    };
    window_show(&window);
}

static void show_intermezzo(void)
{
    window_intermezzo_show(INTERMEZZO_WON, show_end_dialog);
}

void window_mission_end_show_won(void)
{
    mouse_reset_up_state();
    if (scenario_is_tutorial_1() || scenario_is_tutorial_2()) {
        // tutorials: immediately go to next mission
        show_intermezzo();
    } else if (!scenario_is_custom() && scenario_campaign_rank() >= 10) {
        // Won campaign
        window_video_show("smk/win_game.smk", show_intermezzo);
    } else if (has_custom_victory_message()) {
        custom_message_t *victory_message = custom_messages_get(scenario_victory_message());
        const uint8_t *victory_video = custom_messages_get_video(victory_message);
        if (!victory_video) {
            show_end_dialog();
            return;
        }
        char victory_video_utf8[FILE_NAME_MAX];
        encoding_to_utf8(victory_video, victory_video_utf8, FILE_NAME_MAX, encoding_system_uses_decomposed());
        window_video_show(victory_video_utf8, show_end_dialog);
    } else if (campaign_is_active() && !campaign_get_next_mission(scenario_campaign_mission())) {
        // Won campaign
        window_video_show("smk/win_game.smk", show_intermezzo);
    } else {
        if (setting_victory_video()) {
            window_video_show("smk/victory_balcony.smk", show_intermezzo);
        } else {
            window_video_show("smk/victory_senate.smk", show_intermezzo);
        }
    }
}

void window_mission_end_show_fired(void)
{
    window_intermezzo_show(INTERMEZZO_FIRED, show_end_dialog);
}
