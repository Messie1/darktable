cmake_minimum_required(VERSION 2.6)

include_directories("${CMAKE_CURRENT_BINARY_DIR}/../../" "${CMAKE_CURRENT_SOURCE_DIR}")
include_directories(SYSTEM "${PNG_PNG_INCLUDE_DIR}")
set(MODULES copy jpeg png ppm pfm tiff )

add_library(copy MODULE "copy.c")
add_library(jpeg MODULE "jpeg.c")
add_library(png MODULE "png.c")
add_library(ppm MODULE "ppm.c")
add_library(pfm MODULE "pfm.c")
add_library(tiff MODULE "tiff.c")

if(WEBP_FOUND)
        list(APPEND MODULES "webp")
        add_library(webp MODULE "webp.c")
endif(WEBP_FOUND)

if(OPENEXR_FOUND)
	list(APPEND MODULES "exr")
	add_library(exr MODULE "exr.cc")
endif(OPENEXR_FOUND)

if(OPENJPEG_FOUND)
	list(APPEND MODULES "j2k")
	add_library(j2k MODULE "j2k.c")
endif(OPENJPEG_FOUND)

foreach(module ${MODULES})
	target_link_libraries(${module} ${LIBS})
	install(TARGETS  ${module} DESTINATION ${LIB_INSTALL}/darktable/plugins/imageio/format)
endforeach(module)

