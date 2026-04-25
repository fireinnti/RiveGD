#include "rive_file_instance_editor_plugin.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>

// 8 handles: top-left, top-center, top-right, right-center,
//            bottom-right, bottom-center, bottom-left, left-center
// frac_x/frac_y in [0,1] within the rect; affects_x/y = which axes this handle controls
struct HandleDef {
    float fx, fy;
    bool ax, ay;
};
static const HandleDef HANDLES[8] = {
    {0.0f, 0.0f, true,  true },  // 0: top-left
    {0.5f, 0.0f, false, true },  // 1: top-center
    {1.0f, 0.0f, true,  true },  // 2: top-right
    {1.0f, 0.5f, true,  false},  // 3: right-center
    {1.0f, 1.0f, true,  true },  // 4: bottom-right
    {0.5f, 1.0f, false, true },  // 5: bottom-center
    {0.0f, 1.0f, true,  true },  // 6: bottom-left
    {0.0f, 0.5f, true,  false},  // 7: left-center
};

// ---- RiveFileInstanceEditor ----

void RiveFileInstanceEditor::_bind_methods() {}
void RiveFileInstanceEditor::_notification(int p_what) {}

RiveFileInstanceEditor::RiveFileInstanceEditor() {
    top_hb = memnew(HBoxContainer);
}

void RiveFileInstanceEditor::edit(RiveFileInstance *p_node) {
    node = p_node;
}

// ---- RiveFileInstanceEditorPlugin ----

void RiveFileInstanceEditorPlugin::_bind_methods() {}

void RiveFileInstanceEditorPlugin::_enter_tree() {
    rive_editor = memnew(RiveFileInstanceEditor);
    EditorInterface::get_singleton()->get_base_control()->add_child(rive_editor);
    add_control_to_container(CONTAINER_CANVAS_EDITOR_MENU, rive_editor->get_top_hb());
    rive_editor->get_top_hb()->hide();
}

void RiveFileInstanceEditorPlugin::_exit_tree() {
    if (rive_editor) {
        remove_control_from_container(CONTAINER_CANVAS_EDITOR_MENU, rive_editor->get_top_hb());
        if (rive_editor->get_parent()) {
            rive_editor->get_parent()->remove_child(rive_editor);
        }
        memdelete(rive_editor);
        rive_editor = nullptr;
    }
}

bool RiveFileInstanceEditorPlugin::_handles(Object *p_object) const {
    return Object::cast_to<RiveFileInstance>(p_object) != nullptr;
}

void RiveFileInstanceEditorPlugin::_make_visible(bool p_visible) {
    if (p_visible) {
        rive_editor->get_top_hb()->show();
    } else {
        rive_editor->get_top_hb()->hide();
        rive_editor->edit(nullptr);
        dragging_handle = -1;
        hovering_handle = -1;
    }
}

void RiveFileInstanceEditorPlugin::_edit(Object *p_object) {
    rive_editor->edit(Object::cast_to<RiveFileInstance>(p_object));
    dragging_handle = -1;
    hovering_handle = -1;
    update_overlays();
}

// ---------- helpers ----------

Transform2D RiveFileInstanceEditorPlugin::get_viewport_to_parent_xform(RiveFileInstance *node) const {
    // parent_global = global_xform * inv(local_xform)
    Transform2D parent_global = node->get_global_transform() * node->get_transform().affine_inverse();
    return (node->get_viewport_transform() * parent_global).affine_inverse();
}

Vector2 RiveFileInstanceEditorPlugin::get_handle_viewport_pos(int idx, RiveFileInstance *node) const {
    Rect2 rect = node->get_rive_bounds();
    Vector2 local_pos = rect.position + Vector2(rect.size.x * HANDLES[idx].fx,
                                                rect.size.y * HANDLES[idx].fy);
    Transform2D xform = node->get_viewport_transform() * node->get_global_transform();
    return xform.xform(local_pos);
}

int RiveFileInstanceEditorPlugin::get_hovered_handle(Vector2 vp_mouse, RiveFileInstance *node) const {
    for (int i = 0; i < 8; i++) {
        if (vp_mouse.distance_to(get_handle_viewport_pos(i, node)) <= HANDLE_HOVER_RADIUS) {
            return i;
        }
    }
    return -1;
}

// ---------- input ----------

bool RiveFileInstanceEditorPlugin::_forward_canvas_gui_input(const Ref<InputEvent> &p_event) {
    if (!rive_editor || !rive_editor->get_node()) return false;
    RiveFileInstance *node = rive_editor->get_node();

    Ref<InputEventMouseButton> mb = p_event;
    if (mb.is_valid() && mb->get_button_index() == MOUSE_BUTTON_LEFT) {
        if (mb->is_pressed()) {
            int h = get_hovered_handle(mb->get_position(), node);
            if (h >= 0) {
                dragging_handle = h;

                Rect2 rect = node->get_rive_bounds();
                // Anchor fraction is opposite of handle fraction
                float afx = 1.0f - HANDLES[h].fx;
                float afy = 1.0f - HANDLES[h].fy;
                Vector2 anchor_local = rect.position + Vector2(rect.size.x * afx, rect.size.y * afy);

                drag_anchor_parent = node->get_transform().xform(anchor_local);
                drag_start_scale = node->get_scale();
                drag_start_pos = node->get_position();
                drag_start_rect = rect;
                return true;
            }
        } else if (dragging_handle >= 0) {
            // Commit undo/redo action on release
            Vector2 final_scale = node->get_scale();
            Vector2 final_pos = node->get_position();
            Vector2 start_scale = drag_start_scale;
            Vector2 start_pos = drag_start_pos;

            EditorUndoRedoManager *ur = get_undo_redo();
            ur->create_action("Resize RiveFileInstance");
            ur->add_do_method(node, "set_scale", final_scale);
            ur->add_do_method(node, "set_position", final_pos);
            ur->add_undo_method(node, "set_scale", start_scale);
            ur->add_undo_method(node, "set_position", start_pos);
            ur->commit_action(false); // false = already applied, don't re-execute do

            dragging_handle = -1;
            update_overlays();
            return true;
        }
    }

    Ref<InputEventMouseMotion> mm = p_event;
    if (mm.is_valid()) {
        if (dragging_handle >= 0) {
            Transform2D vp_to_parent = get_viewport_to_parent_xform(node);
            Vector2 mouse_parent = vp_to_parent.xform(mm->get_position());

            const HandleDef &h = HANDLES[dragging_handle];
            Rect2 rect = drag_start_rect;

            float hfx = h.fx, hfy = h.fy;
            float afx = 1.0f - hfx, afy = 1.0f - hfy;
            Vector2 anchor_local = rect.position + Vector2(rect.size.x * afx, rect.size.y * afy);
            Vector2 handle_local = rect.position + Vector2(rect.size.x * hfx, rect.size.y * hfy);

            Vector2 new_scale = drag_start_scale;
            Vector2 new_pos = drag_start_pos;

            if (h.ax) {
                float denom = handle_local.x - anchor_local.x;
                if (Math::abs(denom) > 0.001f) {
                    float sx = (mouse_parent.x - drag_anchor_parent.x) / denom;
                    if (Math::abs(sx) > 0.01f) {
                        new_scale.x = sx;
                        new_pos.x = drag_anchor_parent.x - anchor_local.x * sx;
                    }
                }
            }
            if (h.ay) {
                float denom = handle_local.y - anchor_local.y;
                if (Math::abs(denom) > 0.001f) {
                    float sy = (mouse_parent.y - drag_anchor_parent.y) / denom;
                    if (Math::abs(sy) > 0.01f) {
                        new_scale.y = sy;
                        new_pos.y = drag_anchor_parent.y - anchor_local.y * sy;
                    }
                }
            }

            node->set_scale(new_scale);
            node->set_position(new_pos);
            update_overlays();
            return true;
        }

        // Hover detection
        int h = get_hovered_handle(mm->get_position(), node);
        if (h != hovering_handle) {
            hovering_handle = h;
            update_overlays();
        }
    }

    return false;
}

// ---------- draw ----------

void RiveFileInstanceEditorPlugin::_forward_canvas_draw_over_viewport(Control *p_overlay) {
    if (!rive_editor) return;
    RiveFileInstance *node = rive_editor->get_node();
    if (!node || !node->is_visible_in_tree()) return;

    Transform2D xform = node->get_viewport_transform() * node->get_global_transform();
    Rect2 rect = node->get_rive_bounds();

    // Selection border
    PackedVector2Array pts;
    pts.push_back(xform.xform(rect.position));
    pts.push_back(xform.xform(rect.position + Vector2(rect.size.x, 0)));
    pts.push_back(xform.xform(rect.position + rect.size));
    pts.push_back(xform.xform(rect.position + Vector2(0, rect.size.y)));
    pts.push_back(xform.xform(rect.position));
    p_overlay->draw_polyline(pts, Color(1, 0.5f, 0), 2.0f);

    // Handles
    const Color COLOR_NORMAL(1, 1, 1);
    const Color COLOR_HOVER(1, 0.8f, 0);
    const Color COLOR_BORDER(0.2f, 0.2f, 0.2f);
    const float HS = HANDLE_HALF_SIZE;

    for (int i = 0; i < 8; i++) {
        Vector2 center = get_handle_viewport_pos(i, node);
        Rect2 sq(center - Vector2(HS, HS), Vector2(HS * 2, HS * 2));
        Color fill = (i == hovering_handle || i == dragging_handle) ? COLOR_HOVER : COLOR_NORMAL;
        p_overlay->draw_rect(sq, fill);
        p_overlay->draw_rect(sq, COLOR_BORDER, false, 1.0f);
    }
}
