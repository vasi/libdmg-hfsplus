INCLUDE(FindZLIB)

IF(NOT APPLE)
	INCLUDE(FindOpenSSL)
	FIND_LIBRARY(CRYPTO_LIBRARIES crypto
		PATHS
		/usr/lib
		/usr/local/lib
	)
ENDIF()

IF(NOT ZLIB_FOUND)
	message(FATAL_ERROR "zlib is required for dmg!")
ENDIF()

include_directories(${ZLIB_INCLUDE_DIR})

find_library(LZFSE_LIBRARY NAMES lzfse REQUIRED)
find_path(LZFSE_INCLUDE_DIR NAMES lzfse.h REQUIRED)
include_directories(${LZFSE_INCLUDE_DIR})

link_directories(${PROJECT_BINARY_DIR}/common ${PROJECT_BINARY_DIR}/hfs)

add_library(dmg adc.c base64.c checksum.c dmgfile.c dmglib.c filevault.c io.c partition.c resources.c udif.c)

IF(APPLE)
	add_definitions(-DHAVE_CRYPT -DUSE_COMMONCRYPTO)
ELSEIF(OPENSSL_FOUND)
	add_definitions(-DHAVE_CRYPT)
	include_directories(${OPENSSL_INCLUDE_DIR})
	target_link_libraries(dmg ${CRYPTO_LIBRARIES})
	IF(WIN32)
		TARGET_LINK_LIBRARIES(dmg gdi32)
	ENDIF()
ENDIF()

target_link_libraries(dmg common hfs ${ZLIB_LIBRARIES})
target_link_libraries(dmg ${LZFSE_LIBRARY})

add_executable(dmg-bin dmg.c)
target_link_libraries(dmg-bin dmg)

set_target_properties(dmg-bin PROPERTIES OUTPUT_NAME "dmg")

install(TARGETS dmg-bin DESTINATION bin)
