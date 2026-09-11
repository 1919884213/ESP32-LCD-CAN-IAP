#include "ui_screen_iap.h"
#include "ui.h"
#include "ui_helpers.h"
#include "Can/Can.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* IAP 页控件指针：状态、进度条、进度百分比标签 */
static lv_obj_t *s_status;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_source_slot;

/* 设置进度条指示器颜色（用于区分不同升级阶段） */
static void set_progress_color(lv_color_t color)
{
    lv_obj_set_style_bg_color(s_progress_bar, color, LV_PART_INDICATOR);
}

/* 源下拉固定 8 项：索引 i -> 逻辑槽 i/2、半区 i%2（…4A,4B）。 */
static W25Q64_Slot_t source_slot_from_index(uint32_t index)
{
    const uint32_t fw = index / 2U;
    const uint32_t half = index % 2U;
    if (fw >= W25Q64_FW_COUNT) return W25Q64_SLOT_STM32_1_A;
    return W25Q64_FwSlotToSlot((W25Q64_FwSlot_t)fw, (uint8_t)half);
}

/* 源下拉固定为 8 项：四个逻辑槽的固定 bin 名 + (A)/(B)，活动半区标 *。 */
static void refresh_source_options(void)
{
    static uint32_t last_rev = 0xFFFFFFFFU;
    const uint32_t rev = W25Q64_GetNameRevision();
    if (rev == last_rev) return;
    last_rev = rev;

    char options[W25Q64_FW_COUNT * 2U * (W25Q64_BIN_NAME_MAX + 16U)];
    size_t used = 0;
    options[0] = '\0';
    for (uint32_t fw = 0; fw < W25Q64_FW_COUNT; ++fw) {
        W25Q64_Slot_t active = W25Q64_SLOT_STM32_1_A;
        const bool has_active =
            W25Q64_GetActiveSlot((W25Q64_FwSlot_t)fw, &active) == ESP_OK;
        for (uint32_t half = 0; half < 2U; ++half) {
            const W25Q64_Slot_t slot =
                W25Q64_FwSlotToSlot((W25Q64_FwSlot_t)fw, (uint8_t)half);
            const bool current = has_active && slot == active;
            const int written = snprintf(
                &options[used], sizeof(options) - used, "%s (%c)%s",
                ui_firmware_name(fw), half == 0U ? 'A' : 'B',
                current ? " *" : "");
            if (written < 0 || (size_t)written >= sizeof(options) - used) break;
            used += (size_t)written;
            if (!(fw == W25Q64_FW_COUNT - 1U && half == 1U)) {
                options[used++] = '\n';
                options[used] = '\0';
            }
        }
    }

    const uint32_t selected = lv_dropdown_get_selected(s_source_slot);
    lv_dropdown_set_options(s_source_slot, options);
    if (selected < W25Q64_FW_COUNT * 2U) {
        lv_dropdown_set_selected(s_source_slot, selected);
    }
}

/* 升级按钮回调：将选中 W25 半区中的 bin 发送给对应节点（1/2）。 */
static void upgrade_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uint32_t node_id = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    Can_iap_start(node_id, source_slot_from_index(lv_dropdown_get_selected(s_source_slot)));
    lv_label_set_text(s_status, Can_iap_status());
}

/* 返回 OTA 主页面 */
static void back_to_ota_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ui_goto_page_ota();
}

/* 创建 IAP 页面控件：源槽选择、两个升级按钮、进度条、状态。 */
void ui_iap_screen_init(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 12, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = lv_button_create(parent);
    lv_obj_set_size(back, 66, 26);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, 0, 0);
    ui_apply_secondary_button_style(back);
    lv_obj_add_event_cb(back, back_to_ota_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = ui_create_value(back, "返回 OTA", ui_font_zh_14());
    lv_obj_set_style_text_color(back_label, lv_color_hex(0x1677C8),
                                LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_obj_t *title = ui_create_title(parent, "节点 IAP");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *info = ui_create_caption(parent, "01 / 固件来源");
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, 30);

    s_source_slot = lv_dropdown_create(parent);
    lv_obj_set_size(s_source_slot, 260, 30);
    lv_obj_set_pos(s_source_slot, 0, 45);
    lv_dropdown_set_options(s_source_slot,
                            "CanDevice1.bin (A)\nCanDevice1.bin (B)\n"
                            "CanDevice2.bin (A)\nCanDevice2.bin (B)\n"
                            "myCanDevice1.bin (A)\nmyCanDevice1.bin (B)\n"
                            "myCanDevice2.bin (A)\nmyCanDevice2.bin (B)");
    lv_dropdown_set_selected(s_source_slot, 0);
    lv_obj_set_style_text_font(s_source_slot, &lv_font_montserrat_14, LV_PART_MAIN);
    ui_apply_input_style(s_source_slot);
    lv_obj_t *source_list = lv_dropdown_get_list(s_source_slot);
    lv_obj_set_style_bg_color(source_list, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(source_list, lv_color_hex(0x17212B), LV_PART_MAIN);
    lv_obj_set_style_border_color(source_list, lv_color_hex(0xC9D7DC), LV_PART_MAIN);
    lv_obj_set_style_border_width(source_list, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(source_list, lv_color_hex(0xE5F0F7), LV_PART_SELECTED);

lv_obj_t *target = ui_create_caption(parent, "02 / 目标节点");
    lv_obj_set_pos(target, 0, 84);

    /* ENV 节点升级按钮（节点 1） */
    lv_obj_t *env_button = lv_button_create(parent);
    lv_obj_set_size(env_button, 132, 42);
    lv_obj_set_pos(env_button, 0, 101);
    ui_apply_primary_button_style(env_button);
    lv_obj_add_event_cb(env_button, upgrade_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1U);
    lv_obj_t *env_label = ui_create_value(env_button, "升级环境", ui_font_zh_14());
    lv_obj_set_style_text_color(env_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(env_label);

    /* MOTION 节点升级按钮（节点 2） */
    lv_obj_t *motion_button = lv_button_create(parent);
    lv_obj_set_size(motion_button, 132, 42);
    lv_obj_set_pos(motion_button, 146, 101);
    ui_apply_primary_button_style(motion_button);
    lv_obj_add_event_cb(motion_button, upgrade_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)2U);
    lv_obj_t *motion_label = ui_create_value(motion_button, "升级运动", ui_font_zh_14());
    lv_obj_set_style_text_color(motion_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(motion_label);

    /* 升级进度条（0~100%） */
    s_progress_bar = lv_bar_create(parent);
    lv_obj_set_size(s_progress_bar, 272, 12);
    lv_obj_set_pos(s_progress_bar, 0, 159);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    ui_apply_progress_style(s_progress_bar, lv_color_hex(0x1677C8));

    /* 进度百分比标签与状态文本 */
    s_progress_label = ui_create_value(parent, "0%", &lv_font_montserrat_14);
    lv_obj_align_to(s_progress_label, s_progress_bar, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    s_status = ui_create_caption(parent, "就绪");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    refresh_source_options();
}

/* 刷新 IAP 页面：读取升级进度与状态，并按阶段着色进度条 */
void ui_iap_screen_update(void)
{
    if (s_status == NULL) return;

    refresh_source_options();

    uint8_t progress = Can_iap_progress();
    can_iap_state_t state = Can_iap_get_state();
    lv_label_set_text(s_status, Can_iap_status());
    lv_bar_set_value(s_progress_bar, progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_progress_label, "%u%%", progress);

    if (state == CAN_IAP_STATE_FAILED) {
        set_progress_color(lv_color_hex(0xE53935));
    } else if (state == CAN_IAP_STATE_SUCCESS) {
        set_progress_color(lv_color_hex(0x33B864));
    } else if (state == CAN_IAP_STATE_VERIFYING || state == CAN_IAP_STATE_ACTIVATING) {
        set_progress_color(lv_color_hex(0x2196F3));
    } else if (state == CAN_IAP_STATE_TRANSFERRING && progress < 30U) {
        set_progress_color(lv_color_hex(0xF4511E));
    } else if (state == CAN_IAP_STATE_TRANSFERRING && progress < 70U) {
        set_progress_color(lv_color_hex(0xFBC02D));
    } else if (state == CAN_IAP_STATE_TRANSFERRING) {
        set_progress_color(lv_color_hex(0x2196F3));
    } else {
        set_progress_color(lv_color_hex(0x707070));
    }
}
