find_package(TBB CONFIG REQUIRED)

autoneg_add_module(adapter_tbb
    SOURCES tbb_task_executor.cpp system_memory.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS TBB::tbb
)
