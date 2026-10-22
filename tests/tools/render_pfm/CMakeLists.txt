# Sichtprüfung echter Scans bis M4 (D-14): PFM durch die Pipeline rechnen
add_executable(autoneg_render_pfm render_pfm.cpp)
target_link_libraries(autoneg_render_pfm PRIVATE autoneg_steps autoneg_backend_cpu)
autoneg_set_warnings(autoneg_render_pfm)
