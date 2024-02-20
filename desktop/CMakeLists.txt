cmake_minimum_required(VERSION 3.16)
project(breezyDesktop C)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_STANDARD 17)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(LIBPLACEBO REQUIRED libplacebo)
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(breezyDesktop
		main.c
		pipewire.c
		mutter/remote_desktop.c
		mutter/screen_cast.c
)

target_include_directories(breezyDesktop
		SYSTEM BEFORE PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/mutter/include
		${PIPEWIRE_INCLUDE_DIRS}
		${LIBPLACEBO_INCLUDE_DIRS}
		${GLIB_INCLUDE_DIRS}
		${SDL2_INCLUDE_DIRS}
)

target_link_libraries(breezyDesktop
		PRIVATE Threads::Threads
				${PIPEWIRE_LIBRARIES}
				${LIBPLACEBO_LIBRARIES}
				${GLIB_LIBRARIES}
				${SDL2_LIBRARIES}
				Vulkan::Vulkan
)
add_definitions(${GLIB_CFLAGS_OTHER} ${LIBPLACEBO_CFLAGS_OTHER})