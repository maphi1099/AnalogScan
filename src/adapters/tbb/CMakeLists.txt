find_package(TBB CONFIG REQUIRED)

autoneg_add_module(adapter_tbb
    SOURCES src/tbb_task_executor.cpp src/system_memory.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS TBB::tbb
)
