/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Option Parsing Definitions                                                 *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

/////////
// String table

#ifdef OPTTABLE_STR_TABLE_CODE

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif
static constexpr char OptionStrTableStorage[] = 
  "\0"
  "-\0"
  "--\0"
  "bundle\0"
  "content\0"
  "deprecated\0"
  "dylib\0"
  "ignored\0"
  "ignored_silently\0"
  "introspect\0"
  "kind\0"
  "libs\0"
  "main\0"
  "object\0"
  "obsolete\0"
  "opts\0"
  "rare\0"
  "resolve\0"
  "symtab\0"
  "undocumented\0"
  "version\0"
  "<input>\0"
  "<unknown>\0"
  "-add_ast_path\0"
  "-add_empty_section\0"
  "-add_linker_option\0"
  "-add_source_version\0"
  "-add_split_seg_info\0"
  "-adhoc_codesign\0"
  "-alias_list\0"
  "-alias\0"
  "-all_load\0"
  "-allow_dead_duplicates\0"
  "-allow_heap_execute\0"
  "-allow_simulator_linking_to_macosx_dylibs\0"
  "-allow_stack_execute\0"
  "-allow_sub_type_mismatches\0"
  "-allowable_client\0"
  "-application_extension\0"
  "-arch_errors_fatal\0"
  "-arch_multiple\0"
  "-arch\0"
  "-A\0"
  "-bind_at_load\0"
  "-bitcode_bundle\0"
  "-bitcode_hide_symbols\0"
  "-bitcode_process_mode\0"
  "-bitcode_symbol_map\0"
  "-bitcode_verify\0"
  "-bridgeos_version_min\0"
  "-bundle_loader\0"
  "-bundle\0"
  "-b\0"
  "-cache_path_lto\0"
  "--call-graph-profile-sort\0"
  "--check-category-conflicts\0"
  "-classic_linker\0"
  "-client_name\0"
  "--color-diagnostics=\0"
  "--color-diagnostics\0"
  "-commons\0"
  "-compatibility_version\0"
  "--cs-profile-generate\0"
  "--cs-profile-path=\0"
  "-current_version\0"
  "-data_const\0"
  "-data_in_code_info\0"
  "--dead-strip-duplicates\0"
  "-dead_strip_dylibs\0"
  "-dead_strip\0"
  "-debug_snapshot\0"
  "-debug_variant\0"
  "--deduplicate-strings\0"
  "-demangle\0"
  "-dependency_info\0"
  "-dependent_dr_info\0"
  "-dirty_data_list\0"
  "-dot\0"
  "-driverkit_version_min\0"
  "-dtrace\0"
  "-dyld_env\0"
  "-dylib_compatibility_version\0"
  "-dylib_current_version\0"
  "-dylib_file\0"
  "-dylib_install_name\0"
  "-dylib\0"
  "-dylinker_install_name\0"
  "-dylinker\0"
  "-dynamic\0"
  "-d\0"
  "-encryptable\0"
  "--end-lib\0"
  "--error-limit=\0"
  "-executable_path\0"
  "-execute\0"
  "-export_dynamic\0"
  "-exported_symbols_list\0"
  "-exported_symbols_order\0"
  "-exported_symbol\0"
  "-e\0"
  "-fatal_warnings\0"
  "-filelist\0"
  "-final_output\0"
  "-fixup_chains_section\0"
  "-fixup_chains\0"
  "-flat_namespace\0"
  "-flto-codegen-only\0"
  "-force_cpusubtype_ALL\0"
  "-force_flat_namespace\0"
  "-force_load_swift_libs\0"
  "-force_load\0"
  "-force_symbol_not_weak\0"
  "-force_symbol_weak\0"
  "-force_symbols_coalesce_list\0"
  "-force_symbols_not_weak_list\0"
  "-force_symbols_weak_list\0"
  "-framework\0"
  "-function_starts\0"
  "-fvmlib\0"
  "-F\0"
  "-headerpad_max_install_names\0"
  "-headerpad\0"
  "--help-hidden\0"
  "-help\0"
  "-hidden-l\0"
  "--icf=\0"
  "--ignore-auto-link-option=\0"
  "--ignore-auto-link-option\0"
  "-ignore_auto_link\0"
  "-ignore_optimization_hints\0"
  "-image_base\0"
  "-init_offsets\0"
  "-init\0"
  "-install_name\0"
  "-interposable_list\0"
  "-interposable\0"
  "-ios_simulator_version_min\0"
  "-ios_version_min\0"
  "-iosmac_version_min\0"
  "-iphoneos_version_min\0"
  "-i\0"
  "--keep-icf-stabs\0"
  "-keep_dwarf_unwind\0"
  "-keep_private_externs\0"
  "-keep_relocs\0"
  "-kext_objects_dir\0"
  "-kexts_use_stubs\0"
  "-kext\0"
  "-lazy-l\0"
  "-lazy_framework\0"
  "-lazy_library\0"
  "-load_hidden\0"
  "--lto-CGO\0"
  "--lto-debug-pass-manager\0"
  "--lto-O\0"
  "-lto_library\0"
  "-L\0"
  "-l\0"
  "-maccatalyst_version_min\0"
  "-macos_version_min\0"
  "-macosx_version_min\0"
  "-map\0"
  "-mark_dead_strippable_dylib\0"
  "-max_default_common_align\0"
  "-max_relative_cache_size_lto\0"
  "-mcpu\0"
  "-merge_zero_fill_sections\0"
  "-mllvm\0"
  "-move_to_ro_segment\0"
  "-move_to_rw_segment\0"
  "-multi_module\0"
  "-multiply_defined_unused\0"
  "-multiply_defined\0"
  "-M\0"
  "-m\0"
  "-needed-l\0"
  "-needed_framework\0"
  "-needed_library\0"
  "-new_linker\0"
  "--no-call-graph-profile-sort\0"
  "--no-color-diagnostics\0"
  "--no-deduplicate-strings\0"
  "--no-pgo-warn-mismatch\0"
  "--no-warn-duplicate-rpath\0"
  "--no-warn-dylib-install-name\0"
  "--no-warn-thin-archive-missing-members\0"
  "-no_adhoc_codesign\0"
  "-no_application_extension\0"
  "-no_arch_warnings\0"
  "-no_branch_islands\0"
  "-no_compact_linkedit\0"
  "-no_compact_unwind\0"
  "-no_data_const\0"
  "-no_data_in_code_info\0"
  "-no_dead_strip_inits_and_terms\0"
  "-no_deduplicate\0"
  "-no_dependent_dr_info\0"
  "-no_dtrace_dof\0"
  "-no_eh_labels\0"
  "-no_encryption\0"
  "-no_exported_symbols\0"
  "-no_fixup_chains\0"
  "-no_function_starts\0"
  "-no_implicit_dylibs\0"
  "-no_inits\0"
  "-no_keep_dwarf_unwind\0"
  "-no_kext_objects\0"
  "-no_new_main\0"
  "-no_objc_category_merging\0"
  "-no_objc_relative_method_lists\0"
  "-no_order_data\0"
  "-no_order_inits\0"
  "-no_pie\0"
  "-no_source_version\0"
  "-no_uuid\0"
  "-no_warn_duplicate_libraries\0"
  "-no_warn_inits\0"
  "-no_weak_exports\0"
  "-no_weak_imports\0"
  "-no_zero_fill_sections\0"
  "-noall_load\0"
  "-nofixprebinding\0"
  "-nomultidefs\0"
  "-non_global_symbols_no_strip_list\0"
  "-non_global_symbols_strip_list\0"
  "-noprebind_all_twolevel_modules\0"
  "-noprebind\0"
  "-noseglinkedit\0"
  "-not_for_dyld_shared_cache\0"
  "-objc_abi_version\0"
  "-objc_category_merging\0"
  "-objc_gc_compaction\0"
  "-objc_gc_only\0"
  "-objc_gc\0"
  "-objc_relative_method_lists\0"
  "-objc_stubs_fast\0"
  "-objc_stubs_small\0"
  "-ObjC\0"
  "-object_path_lto\0"
  "-order_file_statistics\0"
  "-order_file\0"
  "-oso_prefix\0"
  "-O\0"
  "-o\0"
  "-page_align_data_atoms\0"
  "-pagezero_size\0"
  "-pause\0"
  "--pgo-warn-mismatch\0"
  "-pie\0"
  "-platform_version\0"
  "-prebind_all_twolevel_modules\0"
  "-prebind_allow_overlap\0"
  "-prebind\0"
  "-preload\0"
  "--print-dylib-search\0"
  "--print-symbol-order=\0"
  "-print_statistics\0"
  "-private_bundle\0"
  "-prune_after_lto\0"
  "-prune_interval_lto\0"
  "-random_uuid\0"
  "-read_only_relocs\0"
  "-read_only_stubs\0"
  "-reexport-l\0"
  "-reexport_framework\0"
  "-reexport_library\0"
  "-reexported_symbols_list\0"
  "-rename_section\0"
  "-rename_segment\0"
  "--reproduce=\0"
  "--reproduce\0"
  "-reproducible\0"
  "-root_safe\0"
  "-rpath\0"
  "-run_init_lazily\0"
  "-r\0"
  "-save-temps\0"
  "-sdk_version\0"
  "-search_dylibs_first\0"
  "-search_paths_first\0"
  "-sect_diff_relocs\0"
  "-sectalign\0"
  "-sectcreate\0"
  "-section_order\0"
  "-sectobjectsymbols\0"
  "-sectorder_detail\0"
  "-sectorder\0"
  "-seg1addr\0"
  "-seg_addr_table_filename\0"
  "-seg_page_size\0"
  "-segaddr\0"
  "-segalign\0"
  "-segcreate\0"
  "-seglinkedit\0"
  "-segment_order\0"
  "-segprot\0"
  "-segs_read_only_addr\0"
  "-segs_read_write_addr\0"
  "-setuid_safe\0"
  "-simulator_support\0"
  "-single_module\0"
  "-Si\0"
  "-slow_stubs\0"
  "-snapshot_dir\0"
  "-Sn\0"
  "-source_version\0"
  "-Sp\0"
  "-stack_addr\0"
  "-stack_size\0"
  "--start-lib\0"
  "-static\0"
  "--strict-auto-link\0"
  "-sub_library\0"
  "-sub_umbrella\0"
  "-syslibroot\0"
  "-S\0"
  "-s\0"
  "-text_exec\0"
  "--thinlto-cache-policy=\0"
  "--thinlto-emit-imports-files\0"
  "--thinlto-emit-index-files\0"
  "--thinlto-index-only=\0"
  "--thinlto-index-only\0"
  "--thinlto-jobs=\0"
  "--thinlto-object-suffix-replace=\0"
  "--thinlto-prefix-replace=\0"
  "-threaded_starts_section\0"
  "--threads=\0"
  "--time-trace-granularity=\0"
  "--time-trace=\0"
  "--time-trace\0"
  "-trace_symbol_layout\0"
  "-tvos_version_min\0"
  "-twolevel_namespace_hints\0"
  "-twolevel_namespace\0"
  "-t\0"
  "-uikitformac_version_min\0"
  "-umbrella\0"
  "-unaligned_pointers\0"
  "-undefined\0"
  "-unexported_symbols_list\0"
  "-unexported_symbol\0"
  "-upward-l\0"
  "-upward_framework\0"
  "-upward_library\0"
  "-U\0"
  "-u\0"
  "-verbose_deduplicate\0"
  "-verbose_optimization_hints\0"
  "--verbose\0"
  "-version_details\0"
  "-version_load_command\0"
  "--version\0"
  "-v\0"
  "--warn-duplicate-rpath\0"
  "--warn-dylib-install-name\0"
  "--warn-thin-archive-missing-members\0"
  "-warn_commons\0"
  "-warn_compact_unwind\0"
  "-warn_stabs\0"
  "-warn_weak_exports\0"
  "-watchos_version_min\0"
  "-weak-l\0"
  "-weak_framework\0"
  "-weak_library\0"
  "-weak_reference_mismatches\0"
  "-whatsloaded\0"
  "-why_live\0"
  "-why_load\0"
  "-whyload\0"
  "-w\0"
  "-X\0"
  "-x\0"
  "-Y\0"
  "-y\0"
  "-Z\0"
  ;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

static constexpr llvm::StringTable
OptionStrTable = OptionStrTableStorage;
#endif // OPTTABLE_STR_TABLE_CODE

/////////
// Prefixes

#ifdef OPTTABLE_PREFIXES_TABLE_CODE
static constexpr llvm::StringTable::Offset OptionPrefixesTable[] = {
  0 /* prefixes */,
  1 /* prefixes */, 1 /* '-' */,
  2 /* prefixes */, 1 /* '-' */, 3 /* '--' */,
  1 /* prefixes */, 3 /* '--' */
};
#endif // OPTTABLE_PREFIXES_TABLE_CODE

/////////
// Prefix Union

#ifdef OPTTABLE_PREFIXES_UNION_CODE
static constexpr llvm::StringTable::Offset OptionPrefixesUnion[] = {
  1 /* '-' */,   3 /* '--' */
};
#endif // OPTTABLE_PREFIXES_UNION_CODE

/////////
// ValuesCode

#ifdef OPTTABLE_VALUES_CODE
#endif
/////////
// Groups

#ifdef OPTION
OPTION(0 /* no prefixes */, 6 /* bundle */, grp_bundle, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "CREATING A BUNDLE", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 13 /* content */, grp_content, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "ADDITIONAL CONTENT", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 21 /* deprecated */, grp_deprecated, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "DEPRECATED", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 32 /* dylib */, grp_dylib, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "DYNAMIC LIBRARIES (DYLIB)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 38 /* ignored */, grp_ignored, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "IGNORED", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 46 /* ignored_silently */, grp_ignored_silently, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "IGNORED SILENTLY", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 63 /* introspect */, grp_introspect, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "INTROSPECTING THE LINKER", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 74 /* kind */, grp_kind, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "OUTPUT KIND", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 79 /* libs */, grp_libs, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "LIBRARIES", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 74 /* kind */, grp_lld, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "LLD-SPECIFIC", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 84 /* main */, grp_main, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "MAIN EXECUTABLE", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 89 /* object */, grp_object, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "CREATING AN OBJECT FILE", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 96 /* obsolete */, grp_obsolete, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "OBSOLETE", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 105 /* opts */, grp_opts, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "OPTIMIZATIONS", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 110 /* rare */, grp_rare, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "RARELY USED", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 115 /* resolve */, grp_resolve, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "SYMBOL RESOLUTION", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 123 /* symtab */, grp_symtab, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "SYMBOL TABLE", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 130 /* undocumented */, grp_undocumented, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "UNDOCUMENTED", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0 /* no prefixes */, 143 /* version */, grp_version, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "VERSION TARGETING", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)

//////////
// Options

OPTION(0, 151 /* <input> */, INPUT, Input, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(0, 159 /* <unknown> */, UNKNOWN, Unknown, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 169 /* -add_ast_path */, add_ast_path, Separate, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "AST paths will be emitted as STABS", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 183 /* -add_empty_section */, add_empty_section, MultiArg, grp_content, INVALID, nullptr, 0, DefaultVis, 2,
       "Create an empty <section> in <segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <section>", nullptr)
OPTION(1, 202 /* -add_linker_option */, add_linker_option, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 221 /* -add_source_version */, add_source_version, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 241 /* -add_split_seg_info */, add_split_seg_info, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 261 /* -adhoc_codesign */, adhoc_codesign, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Write an ad-hoc code signature to the output file (default for arm64 binaries)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 277 /* -alias_list */, alias_list, Separate, grp_resolve, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Create symbol aliases specified in <file>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 289 /* -alias */, alias, MultiArg, grp_resolve, INVALID, nullptr, 0, DefaultVis, 2,
       "Create a symbol alias with default global visibility", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol_name> <alternate_name>", nullptr)
OPTION(1, 296 /* -all_load */, all_load, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Load all members of all static archive libraries", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 306 /* -allow_dead_duplicates */, allow_dead_duplicates, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 329 /* -allow_heap_execute */, allow_heap_execute, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "On i386, allow any page to execute code", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 349 /* -allow_simulator_linking_to_macosx_dylibs */, allow_simulator_linking_to_macosx_dylibs, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 391 /* -allow_stack_execute */, allow_stack_execute, Flag, grp_main, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Mark stack segment as executable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 412 /* -allow_sub_type_mismatches */, allow_sub_type_mismatches, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Permit mixing objects compiled for different ARM CPU subtypes", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 439 /* -allowable_client */, allowable_client, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specify <name> of a dylib or framework that is allowed to link to this dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 457 /* -application_extension */, application_extension, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output as safe for use in an application extension, and validate that linked dylibs are safe", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 480 /* -arch_errors_fatal */, arch_errors_fatal, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Escalate to errors any warnings about inputs whose architecture does not match the -arch option", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 499 /* -arch_multiple */, arch_multiple, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Augment error and warning messages with the architecture name", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 514 /* -arch */, arch, Separate, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "The architecture (e.g. ppc, ppc64, i386, x86_64)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<arch_name>", nullptr)
OPTION(1, 520 /* -A */, A, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<basefile>", nullptr)
OPTION(1, 523 /* -bind_at_load */, bind_at_load, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Tell dyld to bind all symbols at load time, rather than lazily", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 537 /* -bitcode_bundle */, bitcode_bundle, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded bitcode", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 553 /* -bitcode_hide_symbols */, bitcode_hide_symbols, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded bitcode", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 575 /* -bitcode_process_mode */, bitcode_process_mode, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded bitcode", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 597 /* -bitcode_symbol_map */, bitcode_symbol_map, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded bitcode", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 617 /* -bitcode_verify */, bitcode_verify, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded bitcode", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 633 /* -bridgeos_version_min */, bridgeos_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest bridgeOS version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 655 /* -bundle_loader */, bundle_loader, Separate, grp_bundle, INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve undefined symbols from <executable>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<executable>", nullptr)
OPTION(1, 670 /* -bundle */, bundle, Flag, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "Produce a bundle", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 678 /* -b */, b, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 681 /* -cache_path_lto */, cache_path_lto, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Use <path> as a directory for the incremental LTO cache", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(6, 697 /* --call-graph-profile-sort */, call_graph_profile_sort, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Reorder sections with call graph profile (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 723 /* --check-category-conflicts */, check_category_conflicts, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Check for conflicts between category & class methods", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 750 /* -classic_linker */, classic_linker, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 766 /* -client_name */, client_name, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specifies a <name> this client should match with the -allowable_client <name> in a dependent dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(6, 779 /* --color-diagnostics= */, color_diagnostics_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Use colors in diagnostics (default: auto)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "[auto,always,never]", nullptr)
OPTION(6, 800 /* --color-diagnostics */, color_diagnostics, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Alias for --color-diagnostics=always", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 820 /* -commons */, commons, Separate, grp_resolve, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Resolve tentative definitions in dylibs according to <treatment>: ignore_dylibs, use_dylibs, error (default is ignore_dylibs)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 829 /* -compatibility_version */, compatibility_version, Separate, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Compatibility <version> of this library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(6, 852 /* --cs-profile-generate */, cs_profile_generate, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Perform context sensitive PGO instrumentation", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 874 /* --cs-profile-path= */, cs_profile_path, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Context sensitive profile file path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 893 /* -current_version */, current_version, Separate, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Current <version> of this library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 910 /* -data_const */, data_const, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Force migration of readonly data into __DATA_CONST segment", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 922 /* -data_in_code_info */, data_in_code_info, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit data-in-code information (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 941 /* --dead-strip-duplicates */, dead_strip_duplicates, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not error on duplicate symbols that will be dead stripped.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 965 /* -dead_strip_dylibs */, dead_strip_dylibs, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Remove dylibs that are unreachable by the entry point or exported symbols", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 984 /* -dead_strip */, dead_strip, Flag, grp_opts, INVALID, nullptr, 0, DefaultVis, 0,
       "Remove unreachable functions and data", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 996 /* -debug_snapshot */, debug_snapshot, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1012 /* -debug_variant */, debug_variant, Flag, grp_ignored_silently, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not warn about issues that are only problems for binaries shipping to customers.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 1027 /* --deduplicate-strings */, deduplicate_strings, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable string deduplication", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1049 /* -demangle */, demangle, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Demangle symbol names in diagnostics", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1059 /* -dependency_info */, dependency_info, Separate, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Dump dependency info", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 1076 /* -dependent_dr_info */, dependent_dr_info, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1095 /* -dirty_data_list */, dirty_data_list, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specify data symbols in <path> destined for the __DATA_DIRTY segment", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 1112 /* -dot */, dot, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Write a graph of symbol dependencies to <path> as a .dot file viewable with GraphViz", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 1117 /* -driverkit_version_min */, driverkit_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest DriverKit version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 1140 /* -dtrace */, dtrace, Separate, grp_content, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Enable DTrace static probes according to declarations in <script>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<script>", nullptr)
OPTION(1, 1148 /* -dyld_env */, dyld_env, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Specifies a LC_DYLD_ENVIRONMENT variable value pair.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<dyld_env_var>", nullptr)
OPTION(1, 1158 /* -dylib_compatibility_version */, dylib_compatibility_version, Separate, grp_dylib, compatibility_version, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -compatibility_version", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 1187 /* -dylib_current_version */, dylib_current_version, Separate, grp_dylib, current_version, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -current_version", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 1210 /* -dylib_file */, dylib_file, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specify <current_path> as different from where a dylib normally resides at <install_path>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<install_path:current_path>", nullptr)
OPTION(1, 1222 /* -dylib_install_name */, dylib_install_name, Separate, grp_dylib, install_name, nullptr, 0, DefaultVis, 0,
       "Alias for -install_name", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 1242 /* -dylib */, dylib, Flag, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "Produce a shared library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1249 /* -dylinker_install_name */, dylinker_install_name, Separate, grp_dylib, install_name, nullptr, 0, DefaultVis, 0,
       "Alias for -install_name", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 1272 /* -dylinker */, dylinker, Flag, grp_kind, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Produce a dylinker only used when building dyld", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1282 /* -dynamic */, dynamic, Flag, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "Link dynamically (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1291 /* -d */, d, Flag, grp_object, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Force tentative into real definitions for common symbols", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1294 /* -encryptable */, encryptable, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Generate the LC_ENCRYPTION_INFO load command", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 1307 /* --end-lib */, end_lib, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "End a grouping of objects that should be treated as if they were together in an archive", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 1317 /* --error-limit= */, error_limit_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Maximum number of errors to print before exiting (default: 20)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1332 /* -executable_path */, executable_path, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1349 /* -execute */, execute, Flag, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "Produce a main executable (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1358 /* -export_dynamic */, export_dynamic, Flag, grp_main, INVALID, nullptr, 0, DefaultVis, 0,
       "Preserve all global symbols during LTO and when dead-stripping executables", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1374 /* -exported_symbols_list */, exported_symbols_list, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Symbols specified in <file> remain global, while others become private externs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 1397 /* -exported_symbols_order */, exported_symbols_order, Separate, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specify frequently-used symbols in <file> to optimize symbol exports", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 1421 /* -exported_symbol */, exported_symbol, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "<symbol> remains global, while others become private externs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 1438 /* -e */, e, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Make <symbol> the entry point of an executable (default is \"start\" from crt1.o)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 1441 /* -fatal_warnings */, fatal_warnings, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Treat warnings as errors", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1457 /* -filelist */, filelist, Separate, grp_content, INVALID, nullptr, 0, DefaultVis, 0,
       "Read names of files to link from <file>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 1467 /* -final_output */, final_output, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Specify dylib install name if -install_name is not used; used by compiler driver for multiple -arch arguments", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 1481 /* -fixup_chains_section */, fixup_chains_section, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1503 /* -fixup_chains */, fixup_chains, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit chained fixups", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1517 /* -flat_namespace */, flat_namespace, Flag, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve symbols from all dylibs, both direct and transitive. Do not record source libraries: dyld must re-search at runtime and use the first definition found", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1533 /* -flto-codegen-only */, flto_codegen_only, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1552 /* -force_cpusubtype_ALL */, force_cpusubtype_ALL, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Mark binary as runnable on any PowerPC, ignoring any PowerPC cpu requirements encoded in the object files", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1574 /* -force_flat_namespace */, force_flat_namespace, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Tell dyld to use a flat namespace on this executable and all its dependent dylibs & bundles", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1596 /* -force_load_swift_libs */, force_load_swift_libs, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Apply -force_load to libraries listed in LC_LINKER_OPTIONS whose names start with 'swift'", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1619 /* -force_load */, force_load, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Load all members static archive library at <path>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 1631 /* -force_symbol_not_weak */, force_symbol_not_weak, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1654 /* -force_symbol_weak */, force_symbol_weak, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1673 /* -force_symbols_coalesce_list */, force_symbols_coalesce_list, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1702 /* -force_symbols_not_weak_list */, force_symbols_not_weak_list, Separate, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1731 /* -force_symbols_weak_list */, force_symbols_weak_list, Separate, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1756 /* -framework */, framework, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for <name>.framework/<name> on the framework search path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 1767 /* -function_starts */, function_starts, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Create table of function start addresses (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1784 /* -fvmlib */, fvmlib, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1792 /* -F */, F, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Add dir to the framework search path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<dir>", nullptr)
OPTION(1, 1795 /* -headerpad_max_install_names */, headerpad_max_install_names, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Allocate extra space so all load-command paths can expand to MAXPATHLEN via install_name_tool", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1824 /* -headerpad */, headerpad, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Allocate hex <size> extra space for future expansion of the load commands via install_name_tool (default is 0x20)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<size>", nullptr)
OPTION(6, 1835 /* --help-hidden */, help_hidden, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Display help for hidden options", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(3, 1849 /* -help */, help, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1855 /* -hidden-l */, hidden_l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -l<name>, but load all symbols with hidden visibility", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(6, 1865 /* --icf= */, icf_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Set level for identical code folding (default: none)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "[none,safe,all]", nullptr)
OPTION(6, 1872 /* --ignore-auto-link-option= */, ignore_auto_link_option_eq, Joined, grp_lld, ignore_auto_link_option, nullptr, 0, DefaultVis, 0,
       "Ignore a single auto-linked library or framework. Useful to ignore invalid options that ld64 ignores", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 1899 /* --ignore-auto-link-option */, ignore_auto_link_option, Separate, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1925 /* -ignore_auto_link */, ignore_auto_link, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore LC_LINKER_OPTIONs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1943 /* -ignore_optimization_hints */, ignore_optimization_hints, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore Linker Optimization Hints", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1970 /* -image_base */, image_base, Separate, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Preferred hex load address for a dylib or bundle.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<address>", nullptr)
OPTION(1, 1982 /* -init_offsets */, init_offsets, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Store __TEXT segment offsets of static initializers", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 1996 /* -init */, init, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Run <symbol> as the first initializer in a dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 2002 /* -install_name */, install_name, Separate, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Set an internal install path in a dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2016 /* -interposable_list */, interposable_list, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Access global symbols listed in <path> indirectly", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 2035 /* -interposable */, interposable, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Indirects access to all to exported symbols in a dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2049 /* -ios_simulator_version_min */, ios_simulator_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest iOS simulator version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2076 /* -ios_version_min */, ios_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest iOS version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2093 /* -iosmac_version_min */, iosmac_version_min, Separate, grp_version, maccatalyst_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -maccatalyst_version_min", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2113 /* -iphoneos_version_min */, iphoneos_version_min, Separate, grp_version, ios_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -ios_version_min", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2135 /* -i */, i, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2138 /* --keep-icf-stabs */, keep_icf_stabs, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Generate STABS entries for symbols folded by ICF. These entries can then be used by dsymutil to discover the address range where folded symbols are located.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2155 /* -keep_dwarf_unwind */, keep_dwarf_unwind, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2174 /* -keep_private_externs */, keep_private_externs, Flag, grp_object, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not convert private external symbols to static symbols (only valid with -r)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2196 /* -keep_relocs */, keep_relocs, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Retain section-based relocation records in the output, which are ignored at runtime by dyld", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2209 /* -kext_objects_dir */, kext_objects_dir, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2227 /* -kexts_use_stubs */, kexts_use_stubs, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2244 /* -kext */, kext, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2250 /* -lazy-l */, lazy_l, Joined, grp_deprecated, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is deprecated and is now an alias for -l<path>.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2258 /* -lazy_framework */, lazy_framework, Separate, grp_deprecated, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is deprecated and is now an alias for -framework.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2274 /* -lazy_library */, lazy_library, Separate, grp_deprecated, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is deprecated and is now an alias for regular linking", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 2288 /* -load_hidden */, load_hidden, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Load all symbols from static library with hidden visibility", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(6, 2301 /* --lto-CGO */, lto_CGO, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Set codegen optimization level for LTO (default: 2)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<cgopt-level>", nullptr)
OPTION(6, 2311 /* --lto-debug-pass-manager */, lto_debug_pass_manager, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Debug new pass manager", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2336 /* --lto-O */, lto_O, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Set optimization level for LTO (default: 2)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<opt-level>", nullptr)
OPTION(1, 2344 /* -lto_library */, lto_library, Separate, grp_obsolete, INVALID, nullptr, 0, DefaultVis, 0,
       "Obsolete. LLD supports LTO directly, without using an external dylib.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 2357 /* -L */, L, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Add dir to the library search path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<dir>", nullptr)
OPTION(1, 2360 /* -l */, l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib or lib<name>.a on the library search path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2363 /* -maccatalyst_version_min */, maccatalyst_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest MacCatalyst version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2388 /* -macos_version_min */, macos_version_min, Separate, grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest macOS version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2407 /* -macosx_version_min */, macosx_version_min, Separate, grp_version, macos_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -macos_version_min", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 2427 /* -map */, map, Separate, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Writes all symbols and their addresses to <path>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 2432 /* -mark_dead_strippable_dylib */, mark_dead_strippable_dylib, Flag, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output dylib as dead-strippable: When a client links against it but does not use any of its symbols, the dylib will not be added to the client's list of needed dylibs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2460 /* -max_default_common_align */, max_default_common_align, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Reduce maximum alignment for common symbols to a hex power-of-2 <boundary>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<boundary>", nullptr)
OPTION(1, 2486 /* -max_relative_cache_size_lto */, max_relative_cache_size_lto, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Limit the incremental LTO cache growth to <percent> of free disk, space", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<percent>", nullptr)
OPTION(1, 2515 /* -mcpu */, mcpu, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Processor family target for LTO code generation", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2521 /* -merge_zero_fill_sections */, merge_zero_fill_sections, Flag, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Merge all zeroed data into the __zerofill section", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2547 /* -mllvm */, mllvm, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Options to pass to LLVM", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2554 /* -move_to_ro_segment */, move_to_ro_segment, MultiArg, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Move code symbols listed in <path> to another <segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <path>", nullptr)
OPTION(1, 2574 /* -move_to_rw_segment */, move_to_rw_segment, MultiArg, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Move data symbols listed in <path> to another <segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <path>", nullptr)
OPTION(1, 2594 /* -multi_module */, multi_module, Flag, grp_rare, interposable, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -interposable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2608 /* -multiply_defined_unused */, multiply_defined_unused, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 2633 /* -multiply_defined */, multiply_defined, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 2651 /* -M */, M, Flag, grp_ignored, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is ignored in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2654 /* -m */, m, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2657 /* -needed-l */, needed_l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -l<name>, but link library even if its symbols are not used and -dead_strip_dylibs is active", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2667 /* -needed_framework */, needed_framework, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -framework <name>, but link <name> even if none of its symbols are used and -dead_strip_dylibs is active", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 2685 /* -needed_library */, needed_library, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but link library even if its symbols are not used and -dead_strip_dylibs is active", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 2701 /* -new_linker */, new_linker, Flag, grp_ignored, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is ignored in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2713 /* --no-call-graph-profile-sort */, no_call_graph_profile_sort, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not reorder sections with call graph profile", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2742 /* --no-color-diagnostics */, no_color_diagnostics, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Alias for --color-diagnostics=never", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2765 /* --no-deduplicate-strings */, no_deduplicate_strings, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable string deduplication. This helps uncover cases of comparing string addresses instead of equality and might have a link time performance benefit.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2790 /* --no-pgo-warn-mismatch */, no_pgo_warn_mismatch, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "turn off warnings about profile cfg mismatch", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2813 /* --no-warn-duplicate-rpath */, no_warn_duplicate_rpath, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn if the same -rpath is specified multiple times", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2839 /* --no-warn-dylib-install-name */, no_warn_dylib_install_name, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn on -install_name if -dylib is not passed (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 2868 /* --no-warn-thin-archive-missing-members */, no_warn_thin_archive_missing_members, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn on missing object files referenced by thin archives", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2907 /* -no_adhoc_codesign */, no_adhoc_codesign, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not write an ad-hoc code signature to the output file (default for x86_64 binaries)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2926 /* -no_application_extension */, no_application_extension, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable application extension functionality (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2952 /* -no_arch_warnings */, no_arch_warnings, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Suppresses warnings about inputs whose architecture does not match the -arch option", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2970 /* -no_branch_islands */, no_branch_islands, Flag, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable infra for branches beyond the maximum branch distance.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 2989 /* -no_compact_linkedit */, no_compact_linkedit, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3010 /* -no_compact_unwind */, no_compact_unwind, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3029 /* -no_data_const */, no_data_const, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Block migration of readonly data away from __DATA segment", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3044 /* -no_data_in_code_info */, no_data_in_code_info, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not emit data-in-code information", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3066 /* -no_dead_strip_inits_and_terms */, no_dead_strip_inits_and_terms, Flag, grp_deprecated, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Unnecessary option: initialization and termination are roots of the dead strip graph, so never dead stripped", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3097 /* -no_deduplicate */, no_deduplicate, Flag, grp_opts, icf_eq, "none\0", 0, DefaultVis, 0,
       "Disable code deduplication (synonym for `--icf=none')", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3113 /* -no_dependent_dr_info */, no_dependent_dr_info, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3135 /* -no_dtrace_dof */, no_dtrace_dof, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable dtrace-dof processing (default).", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3150 /* -no_eh_labels */, no_eh_labels, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "In -r mode, suppress .eh labels in the __eh_frame section", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3164 /* -no_encryption */, no_encryption, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not generate the LC_ENCRYPTION_INFO load command", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3179 /* -no_exported_symbols */, no_exported_symbols, Flag, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't export any symbols from the binary, useful for main executables that don't have plugins", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3200 /* -no_fixup_chains */, no_fixup_chains, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit fixup information as classic dyld opcodes", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3217 /* -no_function_starts */, no_function_starts, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not create table of function start addresses", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3237 /* -no_implicit_dylibs */, no_implicit_dylibs, Flag, grp_opts, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not optimize public dylib transitive symbol references", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3257 /* -no_inits */, no_inits, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Fail if the output contains static initializers", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3267 /* -no_keep_dwarf_unwind */, no_keep_dwarf_unwind, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3289 /* -no_kext_objects */, no_kext_objects, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3306 /* -no_new_main */, no_new_main, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3319 /* -no_objc_category_merging */, no_objc_category_merging, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not merge Objective-C categories", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3345 /* -no_objc_relative_method_lists */, no_objc_relative_method_lists, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't emit relative method lists (use traditional representation)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3376 /* -no_order_data */, no_order_data, Flag, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable default reordering of global data accessed at launch time", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3391 /* -no_order_inits */, no_order_inits, Flag, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable default reordering of initializer and terminator functions", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3407 /* -no_pie */, no_pie, Flag, grp_main, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not build a position independent executable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3415 /* -no_source_version */, no_source_version, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3434 /* -no_uuid */, no_uuid, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not generate the LC_UUID load command", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3443 /* -no_warn_duplicate_libraries */, no_warn_duplicate_libraries, Flag, grp_ignored_silently, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not warn if the input contains duplicate library options.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3472 /* -no_warn_inits */, no_warn_inits, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Suppress warnings for static initializers in the output", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3487 /* -no_weak_exports */, no_weak_exports, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Fail if the linked image contains weak external symbols", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3504 /* -no_weak_imports */, no_weak_imports, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Fail if any symbols are weak imports, allowed to be NULL at runtime", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3521 /* -no_zero_fill_sections */, no_zero_fill_sections, Flag, grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Explicitly store zeroed data in the final image", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3544 /* -noall_load */, noall_load, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't load all static members from archives, this is the default, this negates -all_load", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3556 /* -nofixprebinding */, nofixprebinding, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3573 /* -nomultidefs */, nomultidefs, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3586 /* -non_global_symbols_no_strip_list */, non_global_symbols_no_strip_list, Separate, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Specify in <path> the non-global symbols that should remain in the output symbol table", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 3620 /* -non_global_symbols_strip_list */, non_global_symbols_strip_list, Separate, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Specify in <path> the non-global symbols that should be removed from the output symbol table", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 3651 /* -noprebind_all_twolevel_modules */, noprebind_all_twolevel_modules, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3683 /* -noprebind */, noprebind, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3694 /* -noseglinkedit */, noseglinkedit, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3709 /* -not_for_dyld_shared_cache */, not_for_dyld_shared_cache, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Prevent system dylibs from being placed into the dylib shared cache", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3736 /* -objc_abi_version */, objc_abi_version, Separate, grp_ignored_silently, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option only applies to i386 in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3754 /* -objc_category_merging */, objc_category_merging, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Merge Objective-C categories that share the same base class", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3777 /* -objc_gc_compaction */, objc_gc_compaction, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Mark the Objective-C image as compatible with compacting garbage collection", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3797 /* -objc_gc_only */, objc_gc_only, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Verify that all code was compiled with -fobjc-gc-only", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3811 /* -objc_gc */, objc_gc, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Verify that all code was compiled with -fobjc-gc or -fobjc-gc-only", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3820 /* -objc_relative_method_lists */, objc_relative_method_lists, Flag, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit relative method lists (more compact representation)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3848 /* -objc_stubs_fast */, objc_stubs_fast, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Produce larger stubs for Objective-C method calls with fewer jumps (default).", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3865 /* -objc_stubs_small */, objc_stubs_small, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Produce smaller stubs for Objective-C method calls with more jumps.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3883 /* -ObjC */, ObjC, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Load all members of static archives that are an Objective-C class or category.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3889 /* -object_path_lto */, object_path_lto, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Retain any temporary mach-o file in <path> that would otherwise be deleted during LTO", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 3906 /* -order_file_statistics */, order_file_statistics, Flag, grp_introspect, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Logs information about -order_file", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3929 /* -order_file */, order_file, Separate, grp_opts, INVALID, nullptr, 0, DefaultVis, 0,
       "Layout functions and data according to specification in <file>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 3941 /* -oso_prefix */, oso_prefix, Separate, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Remove the prefix <path> from OSO symbols in the debug map", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 3953 /* -O */, O, JoinedOrSeparate, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Optimize output file size", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3956 /* -o */, o, Separate, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "The name of the output file (default: `a.out')", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 3959 /* -page_align_data_atoms */, page_align_data_atoms, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Distribute global variables on separate pages so page used/dirty status can guide creation of an order file to cluster commonly used/dirty globals", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 3982 /* -pagezero_size */, pagezero_size, Separate, grp_main, INVALID, nullptr, 0, DefaultVis, 0,
       "Size of unreadable segment at address zero is hex <size> (default is 4KB on 32-bit and 4GB on 64-bit)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<size>", nullptr)
OPTION(1, 3997 /* -pause */, pause, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 4004 /* --pgo-warn-mismatch */, pgo_warn_mismatch, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "turn on warnings about profile cfg mismatch (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4024 /* -pie */, pie, Flag, grp_main, INVALID, nullptr, 0, DefaultVis, 0,
       "Build a position independent executable (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4029 /* -platform_version */, platform_version, MultiArg, grp_version, INVALID, nullptr, 0, DefaultVis, 3,
       "Platform (e.g., macos, ios, tvos, watchos, xros, bridgeos, mac-catalyst, ios-sim, tvos-sim, watchos-sim, xros-sim, driverkit) and version numbers", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<platform> <min_version> <sdk_version>", nullptr)
OPTION(1, 4047 /* -prebind_all_twolevel_modules */, prebind_all_twolevel_modules, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4077 /* -prebind_allow_overlap */, prebind_allow_overlap, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4100 /* -prebind */, prebind, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4109 /* -preload */, preload, Flag, grp_kind, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Produce an unsegmented binary for embedded systems", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 4118 /* --print-dylib-search */, print_dylib_search, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Print which paths lld searched when trying to find dylibs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 4139 /* --print-symbol-order= */, print_symbol_order_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Print a symbol order specified by --call-graph-profile-sort into the specified file", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4161 /* -print_statistics */, print_statistics, Flag, grp_introspect, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Log the linker's memory and CPU usage", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4179 /* -private_bundle */, private_bundle, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4195 /* -prune_after_lto */, prune_after_lto, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Remove LTO cache entries after <seconds>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<seconds>", nullptr)
OPTION(1, 4212 /* -prune_interval_lto */, prune_interval_lto, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Prune the incremental LTO cache after <seconds> (-1 disables pruning)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<seconds>", nullptr)
OPTION(1, 4232 /* -random_uuid */, random_uuid, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4245 /* -read_only_relocs */, read_only_relocs, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Handle relocations that modify read-only pages according to <treatment> of warning, error, or suppress (i.e., allow)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 4263 /* -read_only_stubs */, read_only_stubs, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "On i386, make the __IMPORT segment of a final linked image read-only", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4280 /* -reexport-l */, reexport_l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -l<name>, but export all symbols of <name> from newly created library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 4292 /* -reexport_framework */, reexport_framework, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -framework <name>, but export all symbols of <name> from the newly created library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 4312 /* -reexport_library */, reexport_library, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but export all symbols of <path> from newly created library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 4330 /* -reexported_symbols_list */, reexported_symbols_list, Separate, grp_resolve, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Symbols from dependent dylibs specified in <file> are reexported by this dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 4355 /* -rename_section */, rename_section, MultiArg, grp_rare, INVALID, nullptr, 0, DefaultVis, 4,
       "Rename <from_segment>/<from_section> as <to_segment>/<to_section>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<from_segment> <from_section> <to_segment> <to_section>", nullptr)
OPTION(1, 4371 /* -rename_segment */, rename_segment, MultiArg, grp_rare, INVALID, nullptr, 0, DefaultVis, 2,
       "Rename <from_segment> as <to_segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<from_segment> <to_segment>", nullptr)
OPTION(6, 4387 /* --reproduce= */, reproduce_eq, Joined, grp_lld, reproduce, nullptr, 0, DefaultVis, 0,
       "Write tar file containing inputs and command to reproduce link", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 4400 /* --reproduce */, reproduce, Separate, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4412 /* -reproducible */, reproducible, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Make the output reproducible by removing timestamps and other non-deterministic data. This is the default behavior.", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4426 /* -root_safe */, root_safe, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set the MH_ROOT_SAFE bit in the mach-o header", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4437 /* -rpath */, rpath, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Add <path> to dyld search list for dylibs with load path prefix `@rpath/'", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 4444 /* -run_init_lazily */, run_init_lazily, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4461 /* -r */, r, Flag, grp_kind, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Merge multiple object files into one, retaining relocations", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4464 /* -save-temps */, save_temps, Flag, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Save intermediate LTO compilation results", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4476 /* -sdk_version */, sdk_version, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4489 /* -search_dylibs_first */, search_dylibs_first, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib on first pass, then for lib<name>.a on second pass through search path (default for Xcode 3 and earlier)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4510 /* -search_paths_first */, search_paths_first, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib and lib<name>.a at each step in traversing search path (default for Xcode 4 and later)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4530 /* -sect_diff_relocs */, sect_diff_relocs, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 4548 /* -sectalign */, sectalign, MultiArg, grp_rare, INVALID, nullptr, 0, DefaultVis, 3,
       "Align <section> within <segment> to hex power-of-2 <boundary>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <section> <boundary>", nullptr)
OPTION(1, 4559 /* -sectcreate */, sectcreate, MultiArg, grp_content, INVALID, nullptr, 0, DefaultVis, 3,
       "Create <section> in <segment> from the contents of <file>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <section> <file>", nullptr)
OPTION(1, 4571 /* -section_order */, section_order, MultiArg, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "With -preload, specify layout sequence of colon-separated <sections> in <segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <sections>", nullptr)
OPTION(1, 4586 /* -sectobjectsymbols */, sectobjectsymbols, MultiArg, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segname> <sectname>", nullptr)
OPTION(1, 4605 /* -sectorder_detail */, sectorder_detail, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4623 /* -sectorder */, sectorder, MultiArg, grp_obsolete, INVALID, nullptr, 0, DefaultVis, 3,
       "Obsolete. Replaced by more general -order_file option", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segname> <sectname> <orderfile>", nullptr)
OPTION(1, 4634 /* -seg1addr */, seg1addr, Separate, grp_opts, image_base, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -image_base", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<address>", nullptr)
OPTION(1, 4644 /* -seg_addr_table_filename */, seg_addr_table_filename, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 4669 /* -seg_page_size */, seg_page_size, MultiArg, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Specifies the page <size> for <segment>. Segment size will be a multiple of its page size", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <size>", nullptr)
OPTION(1, 4684 /* -segaddr */, segaddr, MultiArg, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Specify the starting hex <address> at a 4KiB page boundary for <segment>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <address>", nullptr)
OPTION(1, 4693 /* -segalign */, segalign, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Align all segments to hex power-of-2 <boundary>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<boundary>", nullptr)
OPTION(1, 4703 /* -segcreate */, segcreate, MultiArg, grp_content, sectcreate, nullptr, 0, DefaultVis, 3,
       "Alias for -sectcreate", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <section> <file>", nullptr)
OPTION(1, 4714 /* -seglinkedit */, seglinkedit, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4727 /* -segment_order */, segment_order, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "With -preload, specify layout sequence of colon-separated <segments>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<colon_separated_segment_list>", nullptr)
OPTION(1, 4742 /* -segprot */, segprot, MultiArg, grp_rare, INVALID, nullptr, 0, DefaultVis, 3,
       "Specifies the <max> and <init> virtual memory protection of <segment> as r/w/x/-seg_addr_table path", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<segment> <max> <init>", nullptr)
OPTION(1, 4751 /* -segs_read_only_addr */, segs_read_only_addr, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<address>", nullptr)
OPTION(1, 4772 /* -segs_read_write_addr */, segs_read_write_addr, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<address>", nullptr)
OPTION(1, 4794 /* -setuid_safe */, setuid_safe, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set the MH_SETUID_SAFE bit in the mach-o header", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4807 /* -simulator_support */, simulator_support, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4826 /* -single_module */, single_module, Flag, grp_deprecated, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Unnecessary option: this is already the default", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4841 /* -Si */, Si, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4845 /* -slow_stubs */, slow_stubs, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4857 /* -snapshot_dir */, snapshot_dir, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4871 /* -Sn */, Sn, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4875 /* -source_version */, source_version, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4891 /* -Sp */, Sp, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4895 /* -stack_addr */, stack_addr, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Initialize stack pointer to hex <address> rounded to a page boundary", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<address>", nullptr)
OPTION(1, 4907 /* -stack_size */, stack_size, Separate, grp_main, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Maximum hex stack size for the main thread in a program. (default is 8MB)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<size>", nullptr)
OPTION(6, 4919 /* --start-lib */, start_lib, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Start a grouping of objects that should be treated as if they were together in an archive", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4931 /* -static */, static, Flag, grp_kind, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Link statically", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 4939 /* --strict-auto-link */, strict_auto_link, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Always warn for missing frameworks or libraries if they are loaded via LC_LINKER_OPTIONS", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 4958 /* -sub_library */, sub_library, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Re-export the dylib as <name>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 4971 /* -sub_umbrella */, sub_umbrella, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Re-export the framework as <name>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 4985 /* -syslibroot */, syslibroot, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Prepend <rootdir> to all library and framework search paths", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<rootdir>", nullptr)
OPTION(1, 4997 /* -S */, S, Flag, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Strip debug information (STABS or DWARF) from the output", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5000 /* -s */, s, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5003 /* -text_exec */, text_exec, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Rename __segment TEXT to __TEXT_EXEC for sections __text and __stubs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5014 /* --thinlto-cache-policy= */, thinlto_cache_policy_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Pruning policy for the ThinLTO cache", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5038 /* --thinlto-emit-imports-files */, thinlto_emit_imports_files, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5067 /* --thinlto-emit-index-files */, thinlto_emit_index_files, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5094 /* --thinlto-index-only= */, thinlto_index_only_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5116 /* --thinlto-index-only */, thinlto_index_only, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5137 /* --thinlto-jobs= */, thinlto_jobs_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Number of ThinLTO jobs. Default to --threads=", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5153 /* --thinlto-object-suffix-replace= */, thinlto_object_suffix_replace_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5186 /* --thinlto-prefix-replace= */, thinlto_prefix_replace_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5212 /* -threaded_starts_section */, threaded_starts_section, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5237 /* --threads= */, threads_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Number of threads. '1' disables multi-threading. By default all available hardware threads are used", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5248 /* --time-trace-granularity= */, time_trace_granularity_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Minimum time granularity (in microseconds) traced by time profiler", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5274 /* --time-trace= */, time_trace_eq, Joined, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Record time trace to <file>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(6, 5288 /* --time-trace */, anonymous_0, Flag, grp_lld, time_trace_eq, nullptr, 0, DefaultVis, 0,
       "Record time trace to file next to output", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5301 /* -trace_symbol_layout */, trace_symbol_layout, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Show where and why symbols move, as specified by -move_to_ro_segment, -move_to_rw_segment, -rename_section, and -rename_segment", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5322 /* -tvos_version_min */, tvos_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest tvOS version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 5340 /* -twolevel_namespace_hints */, twolevel_namespace_hints, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5366 /* -twolevel_namespace */, twolevel_namespace, Flag, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Make dyld look up symbols by (dylib,name) pairs (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5386 /* -t */, t, Flag, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Log every file the linker loads: object, archive, and dylib", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5389 /* -uikitformac_version_min */, uikitformac_version_min, Separate, grp_version, maccatalyst_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -maccatalyst_version_min", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 5414 /* -umbrella */, umbrella, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Re-export this dylib through the umbrella framework <name>", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 5424 /* -unaligned_pointers */, unaligned_pointers, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Handle unaligned pointers in __DATA segments according to <treatment>: warning, error, or suppress (default for arm64e is error, otherwise suppress)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 5444 /* -undefined */, undefined, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Handle undefined symbols according to <treatment>: error, warning, suppress, or dynamic_lookup (default is error)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 5455 /* -unexported_symbols_list */, unexported_symbols_list, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Global symbols specified in <file> become private externs", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<file>", nullptr)
OPTION(1, 5480 /* -unexported_symbol */, unexported_symbol, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Global <symbol> becomes private extern", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 5499 /* -upward-l */, upward_l, Joined, grp_libs, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Like -l<name>, but specify dylib as an upward dependency", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 5509 /* -upward_framework */, upward_framework, Separate, grp_libs, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Like -framework <name>, but specify the framework as an upward dependency", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 5527 /* -upward_library */, upward_library, Separate, grp_libs, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Like bare <path>, but specify dylib as an upward dependency", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 5543 /* -U */, U, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Allow <symbol> to have no definition", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 5546 /* -u */, u, Separate, grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Require that <symbol> be defined for the link to succeed", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 5549 /* -verbose_deduplicate */, verbose_deduplicate, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Print function names eliminated by deduplication and the total size of code savings", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5570 /* -verbose_optimization_hints */, verbose_optimization_hints, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5598 /* --verbose */, verbose, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0, nullptr, (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5608 /* -version_details */, version_details, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Print the linker version in JSON form", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5625 /* -version_load_command */, version_load_command, Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5647 /* --version */, version, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Display the version number and exit", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5657 /* -v */, v, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Print the linker version and search paths in addition to linking", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5660 /* --warn-duplicate-rpath */, warn_duplicate_rpath, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn if the same -rpath is specified multiple times (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5683 /* --warn-dylib-install-name */, warn_dylib_install_name, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn on -install_name if -dylib is not passed", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(6, 5709 /* --warn-thin-archive-missing-members */, warn_thin_archive_missing_members, Flag, grp_lld, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn on missing object files referenced by thin archives (default)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5745 /* -warn_commons */, warn_commons, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn when a tentative definition in an object file matches an external symbol in a dylib, which often means \"extern\" is missing from a variable declaration in a header file", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5759 /* -warn_compact_unwind */, warn_compact_unwind, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn for each FDE that cannot compact into the __unwind_info section and must remain in the __eh_frame section", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5780 /* -warn_stabs */, warn_stabs, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn when bad stab symbols inside a BINCL/EINCL prevent optimization", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5792 /* -warn_weak_exports */, warn_weak_exports, Flag, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn if the linked image contains weak external symbols", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5811 /* -watchos_version_min */, watchos_version_min, Separate, grp_version, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Oldest watchOS version for which linked output is usable", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<version>", nullptr)
OPTION(1, 5832 /* -weak-l */, weak_l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -l<name>, but mark library and its references as weak imports", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 5840 /* -weak_framework */, weak_framework, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -framework <name>, but mark framework and its references as weak imports", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<name>", nullptr)
OPTION(1, 5856 /* -weak_library */, weak_library, Separate, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but mark library and its references as weak imports", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<path>", nullptr)
OPTION(1, 5870 /* -weak_reference_mismatches */, weak_reference_mismatches, Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Resolve symbol imports of conflicting weakness according to <treatment> as weak, non-weak, or error (default is non-weak)", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<treatment>", nullptr)
OPTION(1, 5897 /* -whatsloaded */, whatsloaded, Flag, grp_introspect, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Logs only the object files the linker loads", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5910 /* -why_live */, why_live, Separate, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Log a chain of references to <symbol>, for use with -dead_strip", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 5920 /* -why_load */, why_load, Flag, grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Log why each object file is loaded from a static library", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5930 /* -whyload */, whyload, Flag, grp_introspect, why_load, nullptr, 0, DefaultVis, 0,
       "Alias for -why_load", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5939 /* -w */, w, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Suppress all warnings", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5942 /* -X */, X, Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5945 /* -x */, x, Flag, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Exclude non-global symbols from the output symbol table", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
OPTION(1, 5948 /* -Y */, Y, Separate, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<number>", nullptr)
OPTION(1, 5951 /* -y */, y, Joined, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in ld64", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), "<symbol>", nullptr)
OPTION(1, 5954 /* -Z */, Z, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Remove standard directories from the library and framework search paths", (std::array<std::pair<std::array<unsigned, 2>, const char*>, 1>{{ {std::array<unsigned, 2>{{0, 0}}, nullptr} }}), nullptr, nullptr)
#endif // OPTION

#ifdef SIMPLE_ENUM_VALUE_TABLE

struct SimpleEnumValue {
  const char *Name;
  unsigned Value;
};

struct SimpleEnumValueTable {
  const SimpleEnumValue *Table;
  unsigned Size;
};
static const SimpleEnumValueTable SimpleEnumValueTables[] = {};
static const unsigned SimpleEnumValueTablesSize = std::size(SimpleEnumValueTables);
#endif // SIMPLE_ENUM_VALUE_TABLE


