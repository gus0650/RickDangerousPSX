# xrick for the Sony PlayStation (PSn00bSDK)
cmake_minimum_required(VERSION 3.21)

project(
	xrick-psx
	LANGUAGES    C ASM
	VERSION      1.0.0
	DESCRIPTION  "xrick (Rick Dangerous clone) for the PlayStation"
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(XRICK_DATA_ZIP "${PROJECT_SOURCE_DIR}/assets/data.zip" CACHE FILEPATH "xrick data.zip")
set(ASSET_DIR "${PROJECT_BINARY_DIR}/assets")
set(ASSET_OUTPUTS
	${ASSET_DIR}/romfs.bin ${ASSET_DIR}/sndbank.bin
	${ASSET_DIR}/track2.wav ${ASSET_DIR}/track3.wav ${ASSET_DIR}/track4.wav
	${ASSET_DIR}/track5.wav ${ASSET_DIR}/track6.wav ${ASSET_DIR}/track7.wav)

add_custom_command(
	OUTPUT  ${ASSET_OUTPUTS}
	COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/mkassets.py ${XRICK_DATA_ZIP} ${ASSET_DIR}
	DEPENDS ${PROJECT_SOURCE_DIR}/tools/mkassets.py ${XRICK_DATA_ZIP}
	COMMENT "Converting xrick data for the PlayStation"
)
add_custom_target(assets DEPENDS ${ASSET_OUTPUTS})

set(X src/xrick)
set(XRICK_SOURCES
	${X}/control.c ${X}/devtools.c ${X}/draw.c ${X}/e_bomb.c ${X}/e_bonus.c
	${X}/e_box.c ${X}/e_bullet.c ${X}/e_rick.c ${X}/e_sbonus.c ${X}/e_them.c
	${X}/ents.c ${X}/game.c ${X}/maps.c ${X}/rects.c ${X}/res_magic.c
	${X}/resources.c ${X}/scr_gameover.c ${X}/scr_getname.c ${X}/scr_imain.c
	${X}/scr_imap.c ${X}/scr_pause.c ${X}/scr_xrick.c ${X}/scroller.c ${X}/util.c
	${X}/data/img.c ${X}/data/pics.c ${X}/data/sounds.c ${X}/data/sprites.c
	${X}/data/tiles.c
	${X}/system/basic_funcs.c
	${X}/system/system_psx.c ${X}/system/sysmem_psx.c ${X}/system/sysfile_psx.c
	${X}/system/sysvid_psx.c ${X}/system/sysevt_psx.c ${X}/system/syssnd_psx.c
)

psn00bsdk_add_executable(xrick GPREL ${XRICK_SOURCES})
target_include_directories(xrick PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/xrick/3rd_party)
target_compile_definitions(xrick PRIVATE PLATFORM_PSX)
target_compile_options(xrick PRIVATE -std=gnu99 -Wall -Wno-unused-parameter)
add_dependencies(xrick assets)

psn00bsdk_target_incbin(xrick PRIVATE romfs   ${ASSET_DIR}/romfs.bin)
psn00bsdk_target_incbin(xrick PRIVATE sndbank ${ASSET_DIR}/sndbank.bin)

psn00bsdk_add_cd_image(
	iso
	xrick
	iso.xml
	DEPENDS xrick system.cnf assets
)

