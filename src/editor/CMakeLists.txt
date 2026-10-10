# Logic of the pose editor without a window: forward kinematics, the ghost,
# editing a clip and writing it back. The window itself is app/pose_editor.cpp.
fighter_add_module(editor
    SOURCES
        clip_edit.cpp
        clip_writer.cpp
        ghost.cpp
        pose_fk.cpp
    PUBLIC_DEPS
        fighter::core
        fighter::anim
        fighter::rig
    PRIVATE_DEPS
        fighter::combat
        fighter::stats
)
