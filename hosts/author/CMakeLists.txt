# The authoring tool (ADR-0032, SPEC-0040): the operation surface from a
# command line. Tooling only; no client or server closure contains it.
rawframe_host(NAME author OUTPUT rawframe-author
    SOURCES src/main.cpp src/connect.cpp src/hosting.cpp
    DEPS authoring_session authoring host process world_kest kest content scene schema document execution result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME author_usage COMMAND rawframe_host_author)
    set_tests_properties(author_usage PROPERTIES WILL_FAIL TRUE)
    # A new game from the starter template (D780): fresh identities, the
    # refusals, and the game played by four bots in an arena.
    if(TARGET rawframe_host_arena)
        add_test(NAME author_new_game
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/new_game.sh $<TARGET_FILE:rawframe_host_author>
                         $<TARGET_FILE:rawframe_host_arena>
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(author_new_game PROPERTIES
                             PASS_REGULAR_EXPRESSION
                             "\"kind\": \"authoring.new_game\".*\"files\": 10,[ \n]*\"identities\": 18.*identities: none shared.*refused: over a game, and under a bad name.*\"bots\":4,\"admitted\":4,[^\n]*\"sourceFailures\":0,.*\"code\":\"tick_summary\"[^\n]*\"entities\":9}")
    endif()
    # What authors are told of a game's fields (D783).
    add_test(NAME author_field_hints
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/field_hints.sh $<TARGET_FILE:rawframe_host_author>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(author_field_hints PROPERTIES
                         PASS_REGULAR_EXPRESSION
                         "hinted: side 4 to 100 m.*its mesh an asset.*an emitter hinted.*layers named: ground, props.*refused: a range backwards.*refused: a field the component does not have.*refused: a color that is not an unsigned number.*refused: an asset of no kind there is")
    # A scene of the runners game changed through requests: a new entity
    # with a body at a place, stale and failing requests refused whole, and
    # a dry run that writes nothing.
    add_test(NAME author_runners
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_runners.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_runners)
    set_tests_properties(author_runners PROPERTIES PASS_REGULAR_EXPRESSION "authored runners")
    # Runners' scenes migrated when its Age component grows a field, and
    # refused, unchanged, when it loses one the hall gives a value.
    add_test(NAME author_migrate
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_migrate.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_migrate)
    set_tests_properties(author_migrate PROPERTIES PASS_REGULAR_EXPRESSION "migrated runners")
    # Runners' level read through queries, typed by the game's catalog,
    # and a value read set back through a request to no change.
    add_test(NAME author_read
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_read.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_read)
    set_tests_properties(author_read PROPERTIES PASS_REGULAR_EXPRESSION "read runners")
    # Runners' platforms moved and reverted through their instances'
    # patches, and the game started on the changed level.
    add_test(NAME author_instances
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_instances.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_instances)
    set_tests_properties(author_instances PROPERTIES PASS_REGULAR_EXPRESSION "authored runners' instances")
    # Patrol's guards made from nothing, their enum field set by its cases'
    # names, and the game started on the scene made.
    add_test(NAME author_cases
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_cases.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_cases)
    set_tests_properties(author_cases PROPERTIES PASS_REGULAR_EXPRESSION "authored patrol's cases")
    # An authoring session on runners' level: undone to its very bytes,
    # redone, a stale step refused, the file changed under it opened again,
    # and records out of form or naming a scene outside its root refused.
    add_test(NAME author_session
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_session.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_session)
    set_tests_properties(author_session PROPERTIES PASS_REGULAR_EXPRESSION "authored runners in a session")
    # Two authors, each undoing only their own changes (ADR-0085, D879).
    add_test(NAME author_authors
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_authors.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_authors)
    set_tests_properties(author_authors PROPERTIES
                         PASS_REGULAR_EXPRESSION "two authors each undid their own changes in one session")
    # Authors' presence and their claims (ADR-0085, D880).
    add_test(NAME author_claims
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_claims.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_claims)
    set_tests_properties(author_claims PROPERTIES
                         PASS_REGULAR_EXPRESSION "authors present and their claims kept in one session")
    # Claims on a UI document's nodes (ADR-0085, D886).
    add_test(NAME author_node_claims
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_node_claims.sh
                     $<TARGET_FILE:rawframe_host_author> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_node_claims)
    set_tests_properties(author_node_claims PROPERTIES
                         PASS_REGULAR_EXPRESSION "UI nodes claimed and kept from others in one session")
    # The session shared over QUIC with tokens and grants (ADR-0085, D881).
    add_test(NAME author_hosted
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_hosted.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_hosted)
    set_tests_properties(author_hosted PROPERTIES
                         PASS_REGULAR_EXPRESSION "three collaborators shared one hosted session")
    # A cook made at the host brought to a collaborator's machine (D905).
    add_test(NAME author_copies_cooked
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_copies_cooked.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_copies_cooked)
    set_tests_properties(author_copies_cooked PROPERTIES
                         PASS_REGULAR_EXPRESSION "a cook made at the host was brought here whole")
    # An agent a collaborator of the shared session through MCP (D883).
    add_test(NAME author_mcp_joined
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_mcp_joined.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_mcp_joined)
    set_tests_properties(author_mcp_joined PROPERTIES
                         PASS_REGULAR_EXPRESSION "an agent shared a hosted session under its own name")
    add_test(NAME author_styles
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_styles.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_styles)
    set_tests_properties(author_styles PROPERTIES PASS_REGULAR_EXPRESSION "authored stalls' styles in a session")
    # A data document authored in a session (D811): Plaza's action map.
    add_test(NAME author_data
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_data.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_data)
    set_tests_properties(author_data PROPERTIES PASS_REGULAR_EXPRESSION "authored plaza's action map, a skeleton, and a clip in a session")
    # A game's description, its settings, authored in a session (D817).
    add_test(NAME author_game
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_game.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_game)
    set_tests_properties(author_game PROPERTIES PASS_REGULAR_EXPRESSION "authored quarry's description in a session")
    # A material instance authored (D721): Gale's shore, with its base's
    # interface read beside it.
    add_test(NAME author_material
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_material.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_material)
    set_tests_properties(author_material PROPERTIES PASS_REGULAR_EXPRESSION "authored gale's shore in a session")
    add_test(NAME author_ui
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_ui.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_ui)
    set_tests_properties(author_ui PROPERTIES PASS_REGULAR_EXPRESSION "authored stalls' UI document in a session")
    add_test(NAME author_references
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_references.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_references)
    set_tests_properties(author_references PROPERTIES PASS_REGULAR_EXPRESSION "kept stalls' references in a session")
    # A session cooking its game as a long-running operation (D502): the
    # plaza cooked source by source while records are answered, then from
    # the cache; a second cook refused, one stopped as asked, one into the
    # game refused, and one stopped by the session's end.
    add_test(NAME author_cooks
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_cooks.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_cooks)
    # A source's references with the scenes that name it in a field (D892).
    add_test(NAME author_field_references
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_field_references.sh
                     $<TARGET_FILE:rawframe_host_author> $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_field_references)
    set_tests_properties(author_field_references PROPERTIES
                         PASS_REGULAR_EXPRESSION "a source's references named its scenes' fields")
    # A source's import settings read and set through a session (D800), the
    # importer refusing what it does not take, and the copy cooked.
    add_test(NAME author_settings
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_settings.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_settings)
    # Files from outside made the game's assets through a session (D503):
    # a texture and a glTF with its image imported into a copy of the
    # plaza, declared, listed, and cooked; paths out of place refused.
    add_test(NAME author_imports
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_imports.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_imports)
    set_tests_properties(author_imports PROPERTIES
                         PASS_REGULAR_EXPRESSION "imported a texture and a mesh into the plaza and cooked them")
    # A game's files moved, removed, and its folders made through a
    # session, then cooked from where they are (D929).
    add_test(NAME author_files
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_files.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_files)
    set_tests_properties(author_files PROPERTIES
                         PASS_REGULAR_EXPRESSION "moved, removed, and made the plaza's files and folders and cooked it")
    # A blockout authored line by line and cooked as a mesh (D848, D849).
    add_test(NAME author_blockout
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_blockout.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_blockout)
    set_tests_properties(author_blockout PROPERTIES
                         PASS_REGULAR_EXPRESSION "authored a blockout in a session and cooked it as a mesh")
    # A terrain shaped by brush strokes and cooked as a mesh (D854, D855).
    add_test(NAME author_terrain
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_terrain.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_terrain)
    set_tests_properties(author_terrain PROPERTIES
                         PASS_REGULAR_EXPRESSION "shaped a terrain in a session and cooked it as a mesh")
    # A scatter edited line by line and undone (D865).
    add_test(NAME author_scatter
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_scatter.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_scatter)
    set_tests_properties(author_scatter PROPERTIES
                         PASS_REGULAR_EXPRESSION "edited a scatter in a session and undid it")
    # A sequence edited line by line and undone (D869).
    add_test(NAME author_sequence
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_sequence.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_sequence)
    set_tests_properties(author_sequence PROPERTIES
                         PASS_REGULAR_EXPRESSION "edited a sequence in a session and undid it")
    # A Studio extension's commands run headless (D874).
    add_test(NAME author_extension
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_extension.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_extension)
    set_tests_properties(author_extension PROPERTIES
                         PASS_REGULAR_EXPRESSION "ran an extension's commands in a session, its doors its only reach")
    # A tilemap painted and cooked (D858, D860).
    add_test(NAME author_tilemap
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_tilemap.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_tilemap)
    set_tests_properties(author_tilemap PROPERTIES
                         PASS_REGULAR_EXPRESSION "painted a tilemap in a session and cooked it")
    set_tests_properties(author_cooks PROPERTIES
                         PASS_REGULAR_EXPRESSION "cooked the plaza through a session, [1-9][0-9]* sources")
    # A mesh's and a material's thumbnails drawn by the thumbnail tool (D899,
    # D900), where the tree has the scene's renderer.
    if(TARGET rawframe_host_thumbnail_tool)
        add_test(NAME author_mesh_thumbnails
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_mesh_thumbnails.sh
                         $<TARGET_FILE:rawframe_host_author> $<TARGET_FILE:rawframe_host_cook_tool>
                         $<TARGET_FILE:rawframe_host_thumbnail_tool> ${PROJECT_SOURCE_DIR}
                         ${CMAKE_CURRENT_BINARY_DIR}/author_mesh_thumbnails)
        set_tests_properties(author_mesh_thumbnails PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION "drew the plaza's meshes and materials as thumbnails through a session")
    endif()
    # An agent's tools over the Model Context Protocol (D773): the plaza
    # changed through the session's operations as tools, failures told as
    # failed calls, a batch undone, and the game cooked with its progress
    # and a second cook cancelled.
    add_test(NAME author_mcp
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_mcp.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_mcp)
    set_tests_properties(author_mcp PROPERTIES
                         PASS_REGULAR_EXPRESSION "an agent authored and cooked the plaza over MCP, [1-9][0-9]* sources")
    # A running dedicated server's World read through its tooling endpoint
    # (D408): admitted by its token, status twice as it ticks, an unknown
    # verb answered with an error, and another token refused.
    add_test(NAME author_tooling
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_tooling.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_tooling)
    set_tests_properties(author_tooling PROPERTIES PASS_REGULAR_EXPRESSION "inspected runners through the tooling endpoint")
    # A ray cast into a running World names what it meets and where an
    # author wrote it (D456): the plaza's gate post, by its id in the gate
    # scene.
    if(TARGET rawframe_host_client)
        add_test(NAME author_picks
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_picks.sh $<TARGET_FILE:rawframe_host_author>
                         $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf ${CMAKE_CURRENT_BINARY_DIR}/author_picks)
        set_tests_properties(author_picks PROPERTIES FIXTURES_REQUIRED plaza_content
                             PASS_REGULAR_EXPRESSION "picked the plaza's gate post through the tooling endpoint")
        # A game's scripts debugged through the tooling endpoint (D460): a
        # breakpoint at plaza's stroll stops the server in its tick, the
        # stopped frame read, other verbs refused, and the game carries on.
        add_test(NAME author_debugs
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_debugs.sh $<TARGET_FILE:rawframe_host_author>
                         $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf ${CMAKE_CURRENT_BINARY_DIR}/author_debugs)
        set_tests_properties(author_debugs PROPERTIES FIXTURES_REQUIRED plaza_content
                             PASS_REGULAR_EXPRESSION "stopped the plaza at its stroll system and carried on")
    endif()
    # A scene edited by an authoring session while a dedicated server plays
    # it (D410): the running World makes, writes, and lets go of the crate
    # as the scene changes, read through the tooling endpoint.
    add_test(NAME author_live
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_live.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_live)
    set_tests_properties(author_live PROPERTIES PASS_REGULAR_EXPRESSION "runners' level followed live")
endif()
