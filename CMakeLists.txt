cmake_minimum_required(VERSION 3.16)
# Le nom du projet est en minuscules exprès : project(MEX) créerait une variable MEX_VERSION
# qui masquerait l'option du même nom ci-dessous dès la deuxième configuration.
project(mex VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

option(MEX_BUILD_EDITOR "Compile l'éditeur (Dear ImGui) dans l'exécutable" ON)
option(MEX_SANITIZE "Active AddressSanitizer et UBSan (débogage)" OFF)
option(MEX_BUILD_TEMPLATE "Compile aussi mex-jeu, l'exécutable sans éditeur copié lors de l'export" ON)
option(MEX_VENDORED_SDL "Télécharge SDL2 et la lie statiquement : l'exécutable ne dépend plus d'une SDL2 installée" OFF)
option(MEX_STATIC_RUNTIME "GCC / MinGW : lie statiquement les bibliothèques du compilateur (aucune DLL à fournir sous Windows)" OFF)
option(MEX_WINDOWS_GUI "Windows : application graphique, sans fenêtre de console" ON)
# Vide : le numéro de project() ci-dessus. La valeur par défaut n'est pas copiée dans le cache,
# sinon un dossier de compilation existant garderait l'ancien numéro après une mise à jour.
set(MEX_VERSION "" CACHE STRING "Numéro de version affiché par le moteur (vide : celui du projet)")
if("${MEX_VERSION}" STREQUAL "")
	set(MEX_VERSION "${PROJECT_VERSION}")
endif()

# --- SDL2 -------------------------------------------------------------------
if(EMSCRIPTEN)
	# Export web : seul l'exécutable de jeu est compilé (WebAssembly), avec la SDL2 fournie
	# par Emscripten. Voir « Exporter pour le web » dans le README.
	set(MEX_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
	set(MEX_SDL2_LIBS "")
	add_compile_options(-sUSE_SDL=2)
	add_link_options(-sUSE_SDL=2)
elseif(MEX_VENDORED_SDL)
	# SDL2 est compilée avec le moteur et liée statiquement. Sous Linux, les
	# en-têtes de X11, Wayland, ALSA, PulseAudio... doivent être installés
	# (le paquet libsdl2-dev les apporte tous) ; ces bibliothèques restent
	# chargées dynamiquement à l'exécution.
	include(FetchContent)
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_TEST OFF CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
	FetchContent_Declare(mex_sdl2
		URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
		URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
	)
	FetchContent_MakeAvailable(mex_sdl2)
	set(MEX_SDL2_LIBS SDL2::SDL2-static)
else()
	find_package(SDL2 REQUIRED)
	if(TARGET SDL2::SDL2)
		set(MEX_SDL2_LIBS SDL2::SDL2)
	elseif(TARGET SDL2::SDL2-static)
		set(MEX_SDL2_LIBS SDL2::SDL2-static)
	else()
		# Anciennes distributions : variables au lieu de cibles importées.
		set(MEX_SDL2_LIBS ${SDL2_LIBRARIES})
		include_directories(${SDL2_INCLUDE_DIRS})
	endif()
endif()

# --- Sources ----------------------------------------------------------------
file(GLOB_RECURSE MEX_ENGINE_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/servers/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/modules/*.cpp
)

add_library(mex_engine STATIC ${MEX_ENGINE_SOURCES})
target_include_directories(mex_engine PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_link_libraries(mex_engine PUBLIC ${MEX_SDL2_LIBS})
if(WIN32)
	target_link_libraries(mex_engine PUBLIC ws2_32) # sockets (core/io/net_socket.cpp)
endif()
target_compile_definitions(mex_engine PUBLIC SDL_MAIN_HANDLED MEX_VERSION="${MEX_VERSION}")

if(MSVC)
	target_compile_options(mex_engine PUBLIC /utf-8 /W3 /wd4244 /wd4267 /wd4305)
	target_compile_definitions(mex_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
	target_compile_options(mex_engine PRIVATE -Wall -Wno-unused-parameter)
endif()

if(MEX_SANITIZE AND NOT MSVC)
	target_compile_options(mex_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(mex_engine PUBLIC -fsanitize=address,undefined)
endif()

# --- Licences des composants ----------------------------------------------------
# Leur texte est incorporé à chaque exécutable (éditeur et jeux exportés) : un programme
# distribué en un seul fichier garde ainsi les mentions exigées (option --licences).
set(MEX_LICENCE_LIST
	"SDL2|zlib|thirdparty/sdl2/LICENSE.txt"
	"Godot Engine (parties adaptées)|MIT|thirdparty/godot/LICENSE.txt"
	"stb (image, truetype, vorbis)|MIT ou domaine public|thirdparty/stb/LICENSE.txt"
	"dr_mp3 (dr_libs)|MIT-0 ou domaine public|thirdparty/dr_libs/LICENSE"
	"Police Roboto|Apache 2.0|thirdparty/fonts/LICENSE.txt"
	"Dear ImGui (éditeur)|MIT|thirdparty/imgui/LICENSE.txt"
	"Icônes Lucide (éditeur)|ISC|thirdparty/lucide/LICENSE.txt"
)
set(MEX_LICENCES_CPP "// Produit par CMake depuis thirdparty/*/LICENSE* : ne pas modifier.\n#include \"main/licences.h\"\n")
set(MEX_LICENCE_ROWS "")
set(MEX_LICENCE_INDEX 0)
foreach(licence_entry ${MEX_LICENCE_LIST})
	string(REPLACE "|" ";" licence_parts "${licence_entry}")
	list(GET licence_parts 0 licence_name)
	list(GET licence_parts 1 licence_kind)
	list(GET licence_parts 2 licence_file)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${licence_file}" licence_text)
	string(APPEND MEX_LICENCES_CPP "static const char MEX_LICENCE_TEXT_${MEX_LICENCE_INDEX}[] = R\"MEXLICENCE(${licence_text})MEXLICENCE\";\n")
	string(APPEND MEX_LICENCE_ROWS "\t{ \"${licence_name}\", \"${licence_kind}\", MEX_LICENCE_TEXT_${MEX_LICENCE_INDEX} },\n")
	math(EXPR MEX_LICENCE_INDEX "${MEX_LICENCE_INDEX} + 1")
endforeach()
string(APPEND MEX_LICENCES_CPP "const int MEX_LICENCE_COUNT = ${MEX_LICENCE_INDEX};\nconst MexLicence MEX_LICENCES[] = {\n${MEX_LICENCE_ROWS}\t{ nullptr, nullptr, nullptr },\n};\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/licences.gen.cpp" CONTENT "${MEX_LICENCES_CPP}")

# --- Exécutable ---------------------------------------------------------------
set(MEX_MAIN_SOURCES
	main/main.cpp
	main/game_runner.cpp
	${CMAKE_CURRENT_BINARY_DIR}/licences.gen.cpp
)

if(MEX_BUILD_EDITOR)
	file(GLOB MEX_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp)
	set(MEX_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
	add_library(mex_imgui STATIC
		${MEX_IMGUI_DIR}/imgui.cpp
		${MEX_IMGUI_DIR}/imgui_draw.cpp
		${MEX_IMGUI_DIR}/imgui_tables.cpp
		${MEX_IMGUI_DIR}/imgui_widgets.cpp
		${MEX_IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
		${MEX_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
		${MEX_IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
	)
	target_include_directories(mex_imgui PUBLIC ${MEX_IMGUI_DIR} ${MEX_IMGUI_DIR}/backends)
	target_link_libraries(mex_imgui PUBLIC ${MEX_SDL2_LIBS})
	if(NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
		target_include_directories(mex_imgui PUBLIC ${SDL2_INCLUDE_DIRS})
	endif()
	list(APPEND MEX_MAIN_SOURCES ${MEX_EDITOR_SOURCES})

	# Compétences de MEX Agent : chaque fiche editor/agent/competences/*.md est incorporée à
	# l'éditeur (un seul exécutable, rien à installer à côté).
	file(GLOB MEX_AGENT_SKILL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/editor/agent/competences/*.md")
	list(SORT MEX_AGENT_SKILL_FILES)
	set(MEX_AGENT_SKILLS_CPP "// Produit par CMake depuis editor/agent/competences : ne pas modifier.\n")
	set(MEX_AGENT_SKILL_NAMES "")
	set(MEX_AGENT_SKILL_INDEX 0)
	foreach(skill_file ${MEX_AGENT_SKILL_FILES})
		file(READ "${skill_file}" skill_text)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${skill_file}")
		string(APPEND MEX_AGENT_SKILLS_CPP "static const char MEX_AGENT_SKILL_${MEX_AGENT_SKILL_INDEX}[] = R\"MEXFICHE(${skill_text})MEXFICHE\";\n")
		string(APPEND MEX_AGENT_SKILL_NAMES "\tMEX_AGENT_SKILL_${MEX_AGENT_SKILL_INDEX},\n")
		math(EXPR MEX_AGENT_SKILL_INDEX "${MEX_AGENT_SKILL_INDEX} + 1")
	endforeach()
	string(APPEND MEX_AGENT_SKILLS_CPP "extern const int MEX_AGENT_SKILL_COUNT;\nextern const char *const MEX_AGENT_SKILLS[];\nconst int MEX_AGENT_SKILL_COUNT = ${MEX_AGENT_SKILL_INDEX};\nconst char *const MEX_AGENT_SKILLS[] = {\n${MEX_AGENT_SKILL_NAMES}\tnullptr,\n};\n")
	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/agent_competences.gen.cpp" CONTENT "${MEX_AGENT_SKILLS_CPP}")
	list(APPEND MEX_MAIN_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/agent_competences.gen.cpp")
endif()

if(WIN32)
	enable_language(RC)
	# Le numéro de version peut venir d'une étiquette Git (« 1.2.3-rc1 ») : seuls ses trois
	# premiers nombres vont dans les champs numériques de la ressource.
	set(MEX_RC_MAJOR 0)
	set(MEX_RC_MINOR 0)
	set(MEX_RC_PATCH 0)
	if(MEX_VERSION MATCHES "^([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?")
		set(MEX_RC_MAJOR ${CMAKE_MATCH_1})
		if(NOT "${CMAKE_MATCH_3}" STREQUAL "")
			set(MEX_RC_MINOR ${CMAKE_MATCH_3})
		endif()
		if(NOT "${CMAKE_MATCH_5}" STREQUAL "")
			set(MEX_RC_PATCH ${CMAKE_MATCH_5})
		endif()
	endif()
	# Le fichier de ressources est écrit par CMake : windres transmet mal les chaînes passées
	# par -D, le numéro de version est donc inscrit directement dans le fichier.
	configure_file(platform/windows/mex.rc.in ${CMAKE_CURRENT_BINARY_DIR}/mex.rc @ONLY)
	set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/mex.rc PROPERTIES
		OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/mex.ico)
endif()

# Réglages communs aux exécutables (éditeur et jeu seul).
function(mex_setup_executable target)
	target_link_libraries(${target} PRIVATE mex_engine)
	if(NOT MSVC)
		target_compile_options(${target} PRIVATE -Wall -Wno-unused-parameter)
	endif()
	if(MEX_STATIC_RUNTIME)
		if(MINGW)
			target_link_options(${target} PRIVATE -static)
		elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
		endif()
	endif()
	if(WIN32 AND MEX_WINDOWS_GUI)
		# Pas de console ; main() reste le point d'entrée.
		set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
		if(MSVC)
			target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
		endif()
	endif()
	if(WIN32)
		# Icône et informations de version de l'exécutable (rc de Visual Studio ou windres de MinGW).
		target_sources(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mex.rc)
	endif()
	set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
	foreach(config Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER ${config} config_upper)
		set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/bin)
	endforeach()
endfunction()

add_executable(mex ${MEX_MAIN_SOURCES})
mex_setup_executable(mex)
if(MEX_BUILD_EDITOR)
	# MEX Agent attend la réponse du service dans un fil d'exécution secondaire.
	find_package(Threads REQUIRED)
	target_link_libraries(mex PRIVATE mex_imgui Threads::Threads)
	target_compile_definitions(mex PRIVATE TOOLS_ENABLED)
endif()

if(EMSCRIPTEN)
	# Un seul fichier « mex-jeu.js » (le module WebAssembly y est inclus) : c'est le modèle
	# d'export web, copié par l'éditeur à côté de la page et de l'archive du jeu.
	set_target_properties(mex PROPERTIES OUTPUT_NAME "mex-jeu" SUFFIX ".js")
	target_link_options(mex PRIVATE
		-sSINGLE_FILE=1
		-sALLOW_MEMORY_GROWTH=1
		-sINITIAL_MEMORY=67108864
		-sSTACK_SIZE=4194304
		-sFORCE_FILESYSTEM=1
		-sENVIRONMENT=web
		-sEXPORTED_RUNTIME_METHODS=FS
	)
endif()

# Exécutable de jeu sans éditeur : « Exporter le jeu » le copie à côté de l'archive .pck.
if(MEX_BUILD_EDITOR AND MEX_BUILD_TEMPLATE)
	add_executable(mex-jeu main/main.cpp main/game_runner.cpp ${CMAKE_CURRENT_BINARY_DIR}/licences.gen.cpp)
	mex_setup_executable(mex-jeu)
endif()

# Version console de l'éditeur sous Windows, comme godot.console.exe : un petit lanceur qui
# démarre mex.exe dans le terminal et attend sa fin.
if(WIN32 AND MEX_BUILD_EDITOR)
	add_executable(mex-console platform/windows/console_wrapper_windows.cpp)
	set_target_properties(mex-console PROPERTIES OUTPUT_NAME "mex.console" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
	foreach(config Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER ${config} config_upper)
		set_target_properties(mex-console PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/bin)
	endforeach()
	target_link_libraries(mex-console PRIVATE shlwapi)
	target_sources(mex-console PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mex.rc)
	if(MINGW)
		target_link_options(mex-console PRIVATE -static)
	endif()
endif()

if(WIN32 AND NOT MEX_VENDORED_SDL)
	# Copie SDL2.dll à côté de l'exécutable quand elle est disponible.
	if(TARGET SDL2::SDL2)
		add_custom_command(TARGET mex POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:mex>)
	endif()
endif()
