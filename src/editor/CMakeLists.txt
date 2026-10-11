# Logic of the pose editor without a window: forward kinematics, the ghost,
# editing a clip and writing it back, the clip played by physics (the move
# stand). The window itself is app/pose_editor.cpp.
fighter_add_module(editor
    SOURCES
        arm_ik.cpp
        arm_roles.cpp
        clip_edit.cpp
        clip_writer.cpp
        catalog.cpp
        ghost.cpp
        pose_drag.cpp
        physics_preview.cpp
        pose_fk.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::anim
        fighter::rig
        fighter::stats    # items and fighters (the ghost context, the picker)
        fighter::combat   # the physics preview runs the move stand (combat/move_measure.hpp)
    PRIVATE_DEPS
        fighter::debug
)
