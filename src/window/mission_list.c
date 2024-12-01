#include "mission_list.h"

#include "assets/assets.h"
#include "campaign/campaign.h"
#include "core/image_group.h"
#include "core/log.h"
#include "core/string.h"
#include "game/file.h"
#include "game/file_io.h"
#include "graphics/generic_button.h"
#include "graphics/graphics.h"
#include "graphics/image.h"
#include "graphics/lang_text.h"
#include "graphics/list_box.h"
#include "graphics/panel.h"
#include "graphics/text.h"
#include "graphics/window.h"
#include "input/input.h"
#include "widget/minimap.h"
#include "window/mission_briefing.h"
#include "window/mission_selection.h"
#include "window/new_campaign.h"

#include <string.h>

#define MISSION_PACK_FILE "mission1.pak"
#define MISSION_LIST_Y_POSITION 48
#define NUM_BOTTOM_BUTTONS (sizeof(bottom_buttons) / sizeof(generic_button))
#define NUM_ORIGINAL_SCENARIOS 20
#define MAX_ORIGINAL_SCENARIO_NAME_SIZE 50
#define MAX_ORIGINAL_SCENARIO_DESCRIPTION_SIZE 150
#define MISSION_MAP_MAX_WIDTH 352.0f
#define MISSION_MAP_MAX_HEIGHT 300.0f
#define SELECTED_ITEM_INFO_X_OFFSET 272
#define SELECTED_ITEM_INFO_WIDTH ((int) (MISSION_MAP_MAX_WIDTH))

static void start_scenario(int param1, int param2);
static void button_back(int param1, int param2);
static void draw_item(const list_box_item *item);
static void select_item(unsigned int index, int is_double_click);
static void item_tooltip(const list_box_item *item, tooltip_context *c);

typedef enum {
    ITEM_TYPE_NONE = 0,
    ITEM_TYPE_MISSION = 1,
    ITEM_TYPE_SCENARIO = 2
} item_type;

enum {
    BUTTON_TYPE_BEGIN_SCENARIO = 0,
    BUTTON_TYPE_MISSION_SELECTION = 1
};

typedef struct {
    int index;
    item_type type;
    int rank;
    struct {
        int id;
        int background_image_id;
        const char *background_image_path;
        const uint8_t *title;
        int total_scenarios;
    } mission;
    const campaign_scenario *scenario;
} campaign_item;

static struct {
    campaign_item *items;
    unsigned int total_items;
    int ok_button_type;
    unsigned int bottom_button_focus_id;
    int has_populated_original_scenarios;
    campaign_item *selected_item;
    saved_game_info info;
    int savegame_info_status;
    int campaign_finished;
} data;

static generic_button bottom_buttons[] = {
    {344, 436, 90, 30, button_back, button_none, TR_BUTTON_CANCEL },
    {444, 436, 180, 30, start_scenario, button_none, TR_WINDOW_MISSION_LIST_BUTTON_BEGIN_SCENARIO },
};

static list_box_type list_box = {
    .x = 16,
    .y = MISSION_LIST_Y_POSITION,
    .width_blocks = 15,
    .height_blocks = 24,
    .item_height = 16,
    .draw_inner_panel = 1,
    .extend_to_hidden_scrollbar = 1,
    .decorate_scrollbar = 1,
    .draw_item = draw_item,
    .on_select = select_item,
    .handle_tooltip = item_tooltip
};

static campaign_scenario ORIGINAL_SCENARIOS[NUM_ORIGINAL_SCENARIOS];

static struct {
    int x;
    int y;
    uint8_t name[MAX_ORIGINAL_SCENARIO_NAME_SIZE];
    uint8_t description[MAX_ORIGINAL_SCENARIO_DESCRIPTION_SIZE];
} ORIGINAL_SCENARIO_DATA[NUM_ORIGINAL_SCENARIOS] = {
    { 0, 0 }, { 0, 0 }, { 288, 178 }, { 349, 228 }, { 114, 198 }, { 320, 282 }, { 545, 281 }, { 220, 117 },
    { 169, 105 }, { 236, 288 }, { 572, 279 }, { 15, 312 }, { 93, 236 }, { 152, 55 }, { 123, 296 }, { 575, 323 },
    { 99, 31 }, { 406, 105 }, { 187, 149 }, { 82, 4 }
};

static void clear_list(void)
{
    data.total_items = 0;
    free(data.items);
    data.items = 0;
}

static void generate_original_campaign_list(void)
{
    clear_list();
    data.total_items = 38;
    data.items = malloc(sizeof(campaign_item) * data.total_items);
    data.campaign_finished = 1;
    if (!data.items) {
        log_error("Error creating mission items. The game will probably crash.", 0, 0);
    }
    memset(data.items, 0, sizeof(campaign_item) * data.total_items);
    int mission_id = 1;

    for (int item = 0, rank = 0, scenario = 0; rank < 11; rank++) {

        if (rank > 1) {
            data.items[item].index = item;
            data.items[item].type = ITEM_TYPE_NONE;
            item++;
        }

        int scenarios_per_rank = rank < 2 ? 1 : 2;
        data.items[item].index = item;
        data.items[item].type = ITEM_TYPE_MISSION;
        data.items[item].mission.total_scenarios = scenarios_per_rank;
        data.items[item].mission.background_image_id = rank < 2 ? 0 : (image_group(GROUP_SELECT_MISSION) + rank - 2);
        data.items[item].rank = rank;
        data.items[item].mission.title = lang_get_string(144, 1 + 3 * rank);
        data.items[item].mission.id = mission_id;

        mission_id++;

        if (scenarios_per_rank == 1) {
            data.items[item].scenario = &ORIGINAL_SCENARIOS[scenario];
        } else {
            data.items[item].scenario = 0;
        }
        item++;

        for (int i = 0; i < scenarios_per_rank; i++) {
            int name_offset = rank < 2 ? 0 : 1;
            const uint8_t *name = lang_get_string(144, 1 + 3 * rank + name_offset + i);
            const uint8_t *description = string_find(name, ':');
            int length;

            if (description) {
                description++;
                length = (int) (description - name);
                if (length > MAX_ORIGINAL_SCENARIO_NAME_SIZE) {
                    length = MAX_ORIGINAL_SCENARIO_NAME_SIZE;
                }
                while (*description == ' ') {
                    description++;
                }
            } else {
                length = MAX_ORIGINAL_SCENARIO_NAME_SIZE;
            }
            string_copy(name, ORIGINAL_SCENARIO_DATA[scenario].name, length);
            if (description) {
                length = string_length(description) + 1;
                if (length > MAX_ORIGINAL_SCENARIO_DESCRIPTION_SIZE) {
                    length = MAX_ORIGINAL_SCENARIO_DESCRIPTION_SIZE;
                }
                string_copy(description, ORIGINAL_SCENARIO_DATA[scenario].description, length);

                // Make first letter uppercase
                if (ORIGINAL_SCENARIO_DATA[scenario].description[0] >= 'a' &&
                    ORIGINAL_SCENARIO_DATA[scenario].description[0] <= 'z') {
                    ORIGINAL_SCENARIO_DATA[scenario].description[0] -= 32;
                }
            }
            ORIGINAL_SCENARIOS[scenario].id = scenario;
            ORIGINAL_SCENARIOS[scenario].name = ORIGINAL_SCENARIO_DATA[scenario].name;
            ORIGINAL_SCENARIOS[scenario].description = ORIGINAL_SCENARIO_DATA[scenario].description;
            ORIGINAL_SCENARIOS[scenario].x = ORIGINAL_SCENARIO_DATA[scenario].x;
            ORIGINAL_SCENARIOS[scenario].y = ORIGINAL_SCENARIO_DATA[scenario].y;

            if (scenarios_per_rank == 2) {
                data.items[item].index = item;
                data.items[item].type = ITEM_TYPE_SCENARIO;
                data.items[item].rank = rank;
                data.items[item].scenario = &ORIGINAL_SCENARIOS[scenario];
                item++;
            }

            scenario++;
        }
    }
}

static void generate_custom_campaign_list(void)
{
    clear_list();

    const campaign_info *info = campaign_get_info();

    unsigned int missions_to_show = info->number_of_missions > info->current_mission ?
        info->current_mission + 1 : info->number_of_missions;

    data.campaign_finished = info->number_of_missions == info->current_mission;

    // Add blank space before and after missions with multiple scenarios, except for the top and bottom
    data.total_items = missions_to_show;
    const campaign_mission_info *mission_info = 0;
    unsigned int current_scenario_id = 0;
    for (unsigned int i = 0; i < missions_to_show; i++) {
        int scenarios_on_last_mission = mission_info ? mission_info->total_scenarios : 0;
        mission_info = campaign_get_current_mission(current_scenario_id);
        if (mission_info->total_scenarios > 1) {
            data.total_items += mission_info->total_scenarios;
            if (scenarios_on_last_mission) {
                data.total_items++;
            }
        } else if (scenarios_on_last_mission > 1) {
            data.total_items++;
        }
        current_scenario_id += mission_info->total_scenarios;
    }

    current_scenario_id = 0;
    mission_info = 0;

    data.items = malloc(sizeof(campaign_item) * data.total_items);
    if (!data.items) {
        log_error("Error creating mission items. The game will probably crash.", 0, 0);
    }
    memset(data.items, 0, sizeof(campaign_item) * data.total_items);
    int mission_id = 1;
    int rank = info->starting_rank;
    
    for (int item = 0, mission = 0; mission < missions_to_show; mission++, item++) {
        int scenarios_on_last_mission = mission_info ? mission_info->total_scenarios : 0;

        mission_info = campaign_get_current_mission(current_scenario_id);

        if (scenarios_on_last_mission && (scenarios_on_last_mission > 1 || mission_info->total_scenarios > 1)) {
            data.items[item].index = item;
            data.items[item].type = ITEM_TYPE_NONE;
            item++;
        }

        data.items[item].index = item;
        data.items[item].type = ITEM_TYPE_MISSION;
        data.items[item].mission.total_scenarios = mission_info->total_scenarios;
        data.items[item].mission.background_image_path = mission_info->background_image;
        data.items[item].rank = rank;
        data.items[item].mission.title = mission_info->title;
        data.items[item].mission.id = mission_id;
        mission_id++;

        if (mission_info->total_scenarios == 1) {
            data.items[item].scenario = campaign_get_scenario(current_scenario_id);
            current_scenario_id++;
            continue;
        } else {
            for (int i = 0; i < mission_info->total_scenarios; i++) {
                item++;

                data.items[item].index = item;
                data.items[item].type = ITEM_TYPE_SCENARIO;
                data.items[item].rank = rank;
                data.items[item].scenario = campaign_get_scenario(current_scenario_id);

                current_scenario_id++;
            }
        }

        if (mission_info->next_rank != CAMPAIGN_NO_RANK) {
            rank = mission_info->next_rank;
        }
    }
}

static void init(void)
{
    if (!campaign_is_active()) {
        generate_original_campaign_list();
    } else {
        generate_custom_campaign_list();
    }

    list_box_init(&list_box, data.total_items);

    if (data.total_items > 0) {
        list_box_select_index(&list_box, 0);
    }
}

static void draw_scenario_selection_button(const campaign_scenario *scenario, int x_offset, int y_offset, float scale)
{
    if (!scenario) {
        return;
    }

    if (scenario->x == 0 && scenario->y == 0) {
        return;
    }

    image_draw(image_group(GROUP_SELECT_MISSION_BUTTON),
        x_offset * scale + scenario->x, y_offset * scale + scenario->y, COLOR_MASK_NONE, scale);
}

static int draw_mission_selection_map(void)
{
    int image_id;
    float extra_scale = 1.0f;
    if (!data.selected_item->mission.background_image_id) {
        image_id = image_group(GROUP_EMPIRE_MAP);
        extra_scale = 2.5f;
    } else {
        image_id = data.selected_item->mission.background_image_id;
    }

    const image *img = image_get(image_id);

    float scale_w = img->original.width / extra_scale / MISSION_MAP_MAX_WIDTH;
    float scale_h = img->original.height / extra_scale / MISSION_MAP_MAX_HEIGHT;

    float scale = scale_w > scale_h ? scale_w : scale_h;
    scale *= extra_scale;

    int x_offset = SELECTED_ITEM_INFO_X_OFFSET + ((MISSION_MAP_MAX_WIDTH - img->original.width / scale) / 2);

    image_draw(image_id, x_offset * scale, MISSION_LIST_Y_POSITION * scale,
        COLOR_MASK_NONE, scale);

    for (int i = 1; i <= data.selected_item->mission.total_scenarios; i++) {
        draw_scenario_selection_button(data.items[data.selected_item->index + i].scenario,
            x_offset, MISSION_LIST_Y_POSITION, scale / extra_scale);
    }

    return img->original.height / scale;
}

static void draw_scenario_map(void)
{
    if (data.savegame_info_status == 1) {
        int start_offset = data.info.map_size <= 136 ? 136 : data.info.map_size;
        widget_minimap_draw(310, MISSION_LIST_Y_POSITION - start_offset + data.info.map_size, 266, 272);
    }
}

static void draw_rank(int y_offset)
{
    int x_offset = lang_text_get_width(44, 117, FONT_NORMAL_BLACK) - 6;
    x_offset += text_get_width(string_from_ascii(":"), FONT_NORMAL_BLACK);
    x_offset += lang_text_get_width(32, data.selected_item->rank, FONT_NORMAL_BLACK);
    x_offset = SELECTED_ITEM_INFO_X_OFFSET + (SELECTED_ITEM_INFO_WIDTH - x_offset) / 2;
    x_offset += lang_text_draw(44, 117, x_offset, y_offset, FONT_NORMAL_BLACK) - 6;
    x_offset += text_draw(string_from_ascii(":"), x_offset, y_offset, FONT_NORMAL_BLACK, 0);
    lang_text_draw(32, data.selected_item->rank, x_offset, y_offset, FONT_NORMAL_BLACK);
}

static void draw_background(void)
{
    image_draw_fullscreen_background(image_group(GROUP_EMPIRE_MAP));

    graphics_in_dialog();
    outer_panel_draw(0, 0, 40, 30);
    const uint8_t *title = campaign_is_active() ?
        campaign_get_info()->name : lang_get_string(CUSTOM_TRANSLATION, TR_WINDOW_ORIGINAL_CAMPAIGN_NAME);
    text_draw_centered(title, 32, 14, 554, FONT_LARGE_BLACK, 0);
    lang_text_draw_centered(CUSTOM_TRANSLATION, TR_WINDOW_MISSION_LIST_CAMPAIGN_NOT_FINISHED - data.campaign_finished,
        16, 446, SELECTED_ITEM_INFO_X_OFFSET - 48, FONT_NORMAL_BLACK);

    if (data.selected_item->scenario) {
        draw_scenario_map();
        int y_offset = data.savegame_info_status == 1 && data.info.map_size <= 136 ? data.info.map_size * 2 : 272;
        y_offset += MISSION_LIST_Y_POSITION + 10;
        text_draw_centered(data.selected_item->scenario->name, SELECTED_ITEM_INFO_X_OFFSET, y_offset,
            SELECTED_ITEM_INFO_WIDTH, FONT_LARGE_BLACK, 0);

        if (data.selected_item->type == ITEM_TYPE_MISSION) {
            draw_rank(y_offset + 34);
            y_offset += 20;
        }
        if (data.selected_item->scenario->description) {
            text_draw_multiline(data.selected_item->scenario->description, SELECTED_ITEM_INFO_X_OFFSET, y_offset + 34,
                SELECTED_ITEM_INFO_WIDTH, 1, FONT_NORMAL_BLACK, 0);
        }
        data.ok_button_type = BUTTON_TYPE_BEGIN_SCENARIO;
    } else if (data.selected_item->type == ITEM_TYPE_MISSION) {
        // Selection map
        int y_offset = draw_mission_selection_map() + MISSION_LIST_Y_POSITION + 10;

        // Mission title
        text_draw_centered(data.selected_item->mission.title, SELECTED_ITEM_INFO_X_OFFSET, y_offset,
            SELECTED_ITEM_INFO_WIDTH, FONT_LARGE_BLACK, 0);

        // Rank
        draw_rank(y_offset + 36);

        // Number of missions
        int x_offset = text_get_number_width(data.selected_item->mission.total_scenarios, '@', "", FONT_NORMAL_BLACK);
        x_offset += lang_text_get_width(CUSTOM_TRANSLATION, TR_WINDOW_MISSION_LIST_SCENARIOS, FONT_NORMAL_BLACK);
        x_offset = SELECTED_ITEM_INFO_X_OFFSET + (352 - x_offset) / 2;
        x_offset += text_draw_number(data.selected_item->mission.total_scenarios, '@', "",
            x_offset, y_offset + 56, FONT_NORMAL_BLACK, 0);
        lang_text_draw(CUSTOM_TRANSLATION, TR_WINDOW_MISSION_LIST_SCENARIOS, x_offset, y_offset + 56,
            FONT_NORMAL_BLACK);

        data.ok_button_type = BUTTON_TYPE_MISSION_SELECTION;
    }

    for (int i = 0; i < NUM_BOTTOM_BUTTONS; i++) {
        int text_id = bottom_buttons[i].parameter1;
        if (text_id == TR_WINDOW_MISSION_LIST_BUTTON_BEGIN_SCENARIO) {
            text_id += data.ok_button_type;
        }
        text_draw_centered(lang_get_string(CUSTOM_TRANSLATION, text_id), bottom_buttons[i].x,
            bottom_buttons[i].y + 9, bottom_buttons[i].width, FONT_NORMAL_BLACK, 0);
    }

    list_box_request_refresh(&list_box);

    graphics_reset_dialog();
}

static void draw_item(const list_box_item *item)
{
    const campaign_item *item_to_draw = &data.items[item->index];

    if (item_to_draw->type == ITEM_TYPE_NONE) {
        return;
    }

    font_t font = item_to_draw == data.selected_item ? FONT_NORMAL_WHITE : FONT_NORMAL_GREEN;

    uint8_t text[80];

    if (item_to_draw->type == ITEM_TYPE_SCENARIO) {
        int width = text_draw(string_from_ascii("-"), item->x, item->y + 2, font, 0);
        string_copy(item_to_draw->scenario->name, text, 50);
        text_ellipsize(text, font, item->width - width);
        text_draw(text, item->x + width, item->y + 2, font, 0);
    } else {
        uint8_t *cursor = string_copy(lang_get_string(CUSTOM_TRANSLATION, TR_SAVE_DIALOG_MISSION), text, 80);
        cursor = string_copy(string_from_ascii(" "), cursor, 80 - (int) (cursor - text));
        cursor += string_from_int(cursor, item_to_draw->mission.id, 0);
        cursor = string_copy(string_from_ascii(" - "), cursor, 80 - (int) (cursor - text));
        cursor = string_copy(item_to_draw->mission.title, cursor, 80 - (int) (cursor - text));

        text_draw_ellipsized(text, item->x, item->y, item->width, font, 0);
    }

    if (item->is_focused) {
        button_border_draw(item->x - 4, item->y - 4, item->width + 6, item->height + 4, 1);
    }
}

static void draw_foreground(void)
{
    graphics_in_dialog();
    list_box_draw(&list_box);
    for (int i = 0; i < NUM_BOTTOM_BUTTONS; i++) {
        button_border_draw(bottom_buttons[i].x, bottom_buttons[i].y, bottom_buttons[i].width, bottom_buttons[i].height,
            data.bottom_button_focus_id == i + 1);
    }
    graphics_reset_dialog();
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    const mouse *m_dialog = mouse_in_dialog(m);

    if (generic_buttons_handle_mouse(m_dialog, 0, 0, bottom_buttons, NUM_BOTTOM_BUTTONS, &data.bottom_button_focus_id) ||
        list_box_handle_input(&list_box, m_dialog, 1)) {
        list_box_request_refresh(&list_box);
        return;
    }
    if (input_go_back_requested(m, h)) {
        button_back(0, 0);
    }
}

static void button_back(int param1, int param2)
{
    window_new_campaign_show();
}

static void select_item(unsigned int index, int is_double_click)
{
    if (!is_double_click && (data.items[index].type == ITEM_TYPE_NONE ||
        index == LIST_BOX_NO_SELECTION || data.selected_item == &data.items[index])) {
        return;
    }

    if (is_double_click) {
        start_scenario(0, 0);
        return;
    }

    window_request_refresh();
    data.selected_item = &data.items[index];

    if (data.selected_item->scenario) {
        if (!campaign_is_active()) {
            data.savegame_info_status = game_file_io_read_saved_game_info(MISSION_PACK_FILE,
                game_file_get_original_campaign_mission_offset(data.selected_item->scenario->id), &data.info);
        } else {
            size_t length;
            const campaign_scenario *scenario = data.selected_item->scenario;
            uint8_t *scenario_data = campaign_load_file(scenario->path, &length);
            if (!scenario_data) {
                data.savegame_info_status = 0;
                return;
            }
            buffer buf;
            buffer_init(&buf, scenario_data, (int) length);

            if (file_has_extension(scenario->path, "sav") || file_has_extension(scenario->path, "svx")) {
                data.savegame_info_status = game_file_io_read_saved_game_info_from_buffer(&buf, &data.info);
            } else {
                data.savegame_info_status = game_file_io_read_scenario_info_from_buffer(&buf, &data.info);
            }
            free(scenario_data);
        }
    }
    if (data.selected_item->type == ITEM_TYPE_MISSION && data.selected_item->mission.background_image_path) {
        data.selected_item->mission.background_image_id =
            assets_get_external_image(data.selected_item->mission.background_image_path, 0);
    }
}

static void start_scenario(int param1, int param2)
{
    if (data.selected_item->type == ITEM_TYPE_NONE) {
        return;
    }
    scenario_set_campaign_rank(data.selected_item->rank);
    scenario_set_custom(campaign_is_active() ? 2 : 0);

    if (data.ok_button_type == BUTTON_TYPE_BEGIN_SCENARIO) {
        if (!data.selected_item->scenario) {
            return;
        }
        scenario_set_campaign_mission(data.selected_item->scenario->id);
        window_mission_briefing_show_from_scenario_selection();
    } else {
        window_mission_selection_show();
    }
}

static void item_tooltip(const list_box_item *item, tooltip_context *c)
{
    const campaign_item *item_to_draw = &data.items[item->index];

    if (item_to_draw->type == ITEM_TYPE_NONE) {
        return;
    }

    font_t font = item_to_draw == data.selected_item ? FONT_NORMAL_WHITE : FONT_NORMAL_GREEN;

    if (item_to_draw->type == ITEM_TYPE_SCENARIO) {
        int width = text_get_width(string_from_ascii("-"), font);
        if (text_get_width(item_to_draw->scenario->name, font) + width > item->width) {
            c->precomposed_text = item_to_draw->scenario->name;
            c->type = TOOLTIP_BUTTON;
        }
        return;
    }

    static uint8_t text[300];
    static int last_selection;

    if (last_selection != item->index + 1) {
        uint8_t *cursor = string_copy(lang_get_string(CUSTOM_TRANSLATION, TR_SAVE_DIALOG_MISSION), text, 300);
        cursor = string_copy(string_from_ascii(" "), cursor, 300 - (int) (cursor - text));
        cursor += string_from_int(cursor, item_to_draw->mission.id, 0);
        cursor = string_copy(string_from_ascii(" - "), cursor, 300 - (int) (cursor - text));
        cursor = string_copy(item_to_draw->mission.title, cursor, 300 - (int) (cursor - text));
        last_selection = item->index + 1;
    }

    if (text_get_width(text, font) > item->width) {
        c->precomposed_text = text;
        c->type = TOOLTIP_BUTTON;
    }
}

static void handle_tooltip(tooltip_context *c)
{
    list_box_handle_tooltip(&list_box, c);
}

static void show_window(int keep_data)
{
    window_type window = {
        WINDOW_MISSION_LIST,
        draw_background,
        draw_foreground,
        handle_input,
        handle_tooltip
    };
    if (!keep_data) {
        init();
    }
    window_show(&window);
}

void window_mission_list_show(void)
{
    show_window(0);
}

void window_mission_list_show_again(void)
{
    show_window(1);
}