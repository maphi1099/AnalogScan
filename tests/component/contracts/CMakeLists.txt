# Vertragstests der Ports gegen Fakes und echte Adapter (K-14)
autoneg_add_test(contracts
    LABEL component
    SOURCES contracts_test.cpp repositories_contract_test.cpp
    DEPS adapter_libraw adapter_oiio adapter_lcms adapter_opencv adapter_tbb adapter_json adapter_xxhash test_fakes test_synthetic
)
# Der Test M10_WaitingTaskRunsNoOtherQueuedTask nutzt tbb::parallel_for direkt. Ohne TBB::tbb fehlen
# dem Test die Definitionen der Bibliothek (Debug: TBB_USE_DEBUG); die Inline-Teile von TBB
# passten dann nicht zum Adapter, und die TBB-Laufzeit bricht ab (BUGS.md B-40).
find_package(TBB CONFIG REQUIRED)
target_link_libraries(autoneg_test_contracts PRIVATE TBB::tbb)
