newoption({
	trigger = "graphics",
	value = "OPENGL_VERSION",
	description = "version of OpenGL to build raylib against",
	allowed = {
		{ "opengl11", "OpenGL 1.1" },
		{ "opengl21", "OpenGL 2.1" },
		{ "opengl33", "OpenGL 3.3" },
		{ "opengl43", "OpenGL 4.3" },
		{ "openges2", "OpenGL ES2" },
		{ "openges3", "OpenGL ES3" },
		{ "software", "OpenGL 1.1 Software Render" },
	},
	default = "opengl33",
})

newoption({
	trigger = "backend",
	value = "BACKEND_TYPE",
	description = "Backend Platform to use",
	allowed = {
		{ "glfw", "GLFW" },
		{ "rgfw", "RGFW" },
		{ "win32", "WIN32" },
	},
	default = "glfw",
})

newoption({
	trigger = "wayland",
	value = "WAYLAND",
	description = "build for wayland",
	allowed = {
		{ "off", "Off" },
		{ "on", "On" },
	},
	default = "off",
})

newoption({
	trigger = "with-emscripten",
	description = "Build for web using Emscripten",
})

function download_progress(total, current)
	local ratio = current / total
	ratio = math.min(math.max(ratio, 0), 1)
	local percent = math.floor(ratio * 100)
	print("Download progress (" .. percent .. "%/100%)")
end

function check_raylib()
	os.chdir("external")
	if os.isdir("raylib-master") == false then
		if not os.isfile("raylib-master.zip") then
			print("Raylib not found, downloading from github")
			local result_str, response_code =
				http.download("https://github.com/raysan5/raylib/archive/refs/heads/master.zip", "raylib-master.zip", {
					progress = download_progress,
					headers = { "From: Premake", "Referer: Premake" },
				})
		end
		print("Unzipping to " .. os.getcwd())
		zip.extract("raylib-master.zip", os.getcwd())
		os.remove("raylib-master.zip")
	end
	os.chdir("../")
end

function build_externals()
	print("calling externals")
	check_raylib()
end

function platform_defines()
	filter({ "options:with-emscripten" })
	defines({ "PLATFORM_WEB" })

	filter({ "options:backend=glfw", "options:not with-emscripten" })
	defines({ "PLATFORM_DESKTOP" })

	filter({ "options:backend=rgfw" })
	defines({ "PLATFORM_DESKTOP_RGFW" })

	filter({ "options:backend=win32" })
	defines({ "PLATFORM_DESKTOP_WIN32" })

	filter({ "options:graphics=opengl43" })
	defines({ "GRAPHICS_API_OPENGL_43" })

	filter({ "options:graphics=opengl33" })
	defines({ "GRAPHICS_API_OPENGL_33" })

	filter({ "options:graphics=opengl21" })
	defines({ "GRAPHICS_API_OPENGL_21" })

	filter({ "options:graphics=opengl11" })
	defines({ "GRAPHICS_API_OPENGL_11" })

	filter({ "options:graphics=openges3" })
	defines({ "GRAPHICS_API_OPENGL_ES3" })

	filter({ "options:graphics=openges2" })
	defines({ "GRAPHICS_API_OPENGL_ES2" })

	filter({ "options:graphics=software" })
	defines({ "GRAPHICS_API_OPENGL_11_SOFTWARE" })

	filter({ "system:macosx" })
	disablewarnings({ "deprecated-declarations" })

	filter({ "system:linux", "options:wayland=off" })
	defines({ "_GLFW_X11" })

	filter({ "system:linux", "options:wayland=on" })
	defines({ "_GLFW_WAYLAND" })

	filter({})
end

-- if you don't want to download raylib, then set this to false, and set the raylib dir to where you want raylib to be pulled from, must be full sources.
downloadRaylib = true
local ROOT = path.getabsolute("..")
raylib_dir = path.join(ROOT, "build/external/raylib-master")

workspaceName = "Fluvius"
baseName = path.getbasename(path.getdirectory(os.getcwd()))

--if (baseName ~= 'raylib-quickstart') then
workspaceName = baseName
--end

if os.isdir("build_files") == false then
	os.mkdir("build_files")
end

if os.isdir("external") == false then
	os.mkdir("external")
end

workspace(workspaceName)
location("../")
configurations({ "Debug", "Release" })

filter({ "options:not with-emscripten" })
platforms({ "x64", "x86", "ARM64" })
defaultplatform("x64")

filter({ "options:with-emscripten" })
platforms({ "Web" })
defaultplatform("Web")

filter("configurations:Debug")
defines({ "DEBUG", "DEBUG_MODE", "LOG_LEVEL_DEBUG" })
symbols("On")

filter("configurations:Release")
defines({ "NDEBUG" })
optimize("On")

filter({ "configurations:Release", "action:vs*" })
linktimeoptimization("On")

filter({ "platforms:x64" })
architecture("x86_64")

filter({ "platforms:ARM64" })
architecture("ARM64")

filter({})

targetdir("bin/%{cfg.buildcfg}/")

if downloadRaylib then
	build_externals()
end

startproject(workspaceName)

project(workspaceName)
kind("ConsoleApp")
location("build_files/")
targetdir("../bin/%{cfg.buildcfg}")

filter({ "system:windows", "action:gmake*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })
prebuildcommands({
	'windres "' .. ROOT .. '/src/app/application.rc" -O coff -o "%{cfg.buildtarget.directory}/application_res.o"',
})
linkoptions({
	"%{cfg.buildtarget.directory}/application_res.o",
})

filter({ "system:windows", "action:vs*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })

filter("system:linux")
local binDir = path.getabsolute("../bin/%{cfg.buildcfg}")
postbuildcommands({
	-- Copy the icon
	'cp "'
		.. ROOT
		.. '/resources/icon.png" "'
		.. binDir
		.. '/icon.png"',

	-- Generate desktop file line by line
	'echo "[Desktop Entry]" > "'
		.. binDir
		.. '/Fluvius.desktop"',
	'echo "Name=Fluvius" >> "' .. binDir .. '/Fluvius.desktop"',
	'echo "Exec=' .. binDir .. '/Fluvius" >> "' .. binDir .. '/Fluvius.desktop"',
	'echo "Icon=' .. binDir .. '/icon.png" >> "' .. binDir .. '/Fluvius.desktop"',
	'echo "Type=Application" >> "' .. binDir .. '/Fluvius.desktop"',
	'echo "Categories=Graphics;" >> "' .. binDir .. '/Fluvius.desktop"',
	'chmod +x "' .. binDir .. '/Fluvius.desktop"',
})

filter({ "system:macosx" })
postbuildcommands({
	'cp "' .. ROOT .. '/resources/icon.png" "%{cfg.targetdir}/icon.png"',
})

filter({})

-- Emscripten Web Build Configuration
filter({ "options:with-emscripten" })
kind("ConsoleApp")
targetextension(".html")
buildoptions({
	"-sGL_ENABLE_GET_PROC_ADDRESS",
})
linkoptions({
	"-s USE_GLFW=3",
	"-s ASYNCIFY",
	"-s TOTAL_MEMORY=67108864",
	"-s FORCE_FILESYSTEM=1",
	"-s ALLOW_MEMORY_GROWTH=1",
	"-s EXPORTED_FUNCTIONS=['_main']",
	"-s EXPORTED_RUNTIME_METHODS=ccall",
	"-s OFFSCREENCANVAS_SUPPORT=1 ",
	"-s GL_EMULATE_GLES_VERSION_STRING_FORMAT=1 ",
	"--no-heap-copy",
	"--preload-file ../../resources@/resources",
	"--shell-file ../../src/app/index.html",
	"-s FULL_ES2=1",
	"-sGL_ENABLE_GET_PROC_ADDRESS",
})

filter({ "options:with-emscripten", "configurations:Release" })
buildoptions({ "-Os" })
linkoptions({ "-Os" })

filter({ "options:with-emscripten", "configurations:Debug" })
buildoptions({ "-g" })
linkoptions({ "-g", "-s ASSERTIONS=1" })

filter({ "system:windows", "configurations:Release", "action:gmake*", "options:not with-emscripten" })
kind("WindowedApp")
buildoptions({ "-Wl,--subsystem,windows" })

filter({ "system:windows", "configurations:Release", "action:vs*" })
kind("WindowedApp")
entrypoint("mainCRTStartup")

filter("action:vs*")
debugdir("$(SolutionDir)")

filter({ "action:gmake*" }) -- Uncoment if you need to force StaticLib
--          buildoptions { "-static" }
filter({})

vpaths({
	["Header Files/*"] = { "../include/**.h", "../include/**.hpp", "../src/**.h", "../src/**.hpp" },
	["Source Files/*"] = { "../src/**.c", "src/**.cpp" },
	["Windows Resource Files/*"] = { "../src/app/**.rc", "src/app/**.ico" },
})

files({ "../src/**.c", "../src/**.cpp", "../src/**.h", "../src/**.hpp", "../include/**.h", "../include/**.hpp" })

-- For web builds, include raylib source files directly
filter({ "options:with-emscripten" })
files({
	raylib_dir .. "/src/rcore.c",
	raylib_dir .. "/src/rshapes.c",
	raylib_dir .. "/src/rtextures.c",
	raylib_dir .. "/src/rtext.c",
	raylib_dir .. "/src/rmodels.c",
	raylib_dir .. "/src/raudio.c",
})
defines({ "PLATFORM_WEB" })

filter({ "system:windows", "action:vs*", "options:not with-emscripten" })
files({ "../src/app/*.rc", "../src/app/*.ico" })

filter({})

includedirs({ "../src" })
includedirs({ "../include" })

filter({ "options:not with-emscripten" })
links({ "raylib" })

filter({})

filter({ "options:with-emscripten" })
cdialect("gnu17")
cppdialect("gnu++17")

filter({ "options:not with-emscripten" })
cdialect("C17")
cppdialect("C++17")

filter({})

includedirs({ raylib_dir .. "/src" })

filter({ "toolset:gcc or clang" })
buildoptions({ "-Wshadow" })

filter("action:vs*")
buildoptions({ "/w34456" })

filter({})
platform_defines()

filter("action:vs*")
defines({ "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS" })
dependson({ "raylib" })
links({ "raylib.lib" })
characterset("Unicode")
buildoptions({ "/Zc:__cplusplus" })

filter({ "system:windows", "options:not with-emscripten" })
defines({ "_WIN32" })
links({ "winmm", "gdi32", "opengl32" })
libdirs({ "../bin/%{cfg.buildcfg}" })

filter({ "system:linux", "options:not with-emscripten" })
links({ "pthread", "m", "dl", "rt" })

filter({ "system:linux", "options:wayland=off", "options:not with-emscripten" })
links({ "X11" })

filter({ "system:linux", "options:wayland=on", "options:not with-emscripten" })
links({ "wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon" })

filter({ "system:macosx", "options:not with-emscripten" })
links({
	"OpenGL.framework",
	"Cocoa.framework",
	"IOKit.framework",
	"CoreFoundation.framework",
	"CoreAudio.framework",
	"CoreVideo.framework",
	"AudioToolbox.framework",
})

filter({})

-- Only build raylib library for non-web builds
if not _OPTIONS["with-emscripten"] then
	group("Dependencies")

	project("raylib")
	kind("StaticLib")

	platform_defines()

	location("build_files/")

	language("C")
	targetdir("../bin/%{cfg.buildcfg}")

	filter({ "options:wayland=on" })
	defines({ "GLFW_LINUX_ENABLE_WAYLAND=TRUE" })

	filter({ "options:wayland=on", "system:linux" })
	prebuildcommands({
		"@echo Generating Wayland protocols...",
		-- Core Wayland & Shell
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/wayland.xml ../"
			.. raylib_dir
			.. "/src/wayland-client-protocol.h",
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-shell.xml ../"
			.. raylib_dir
			.. "/src/xdg-shell-client-protocol.h",
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-decoration-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/xdg-decoration-unstable-v1-client-protocol.h",

		-- Viewporter
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/viewporter.xml ../"
			.. raylib_dir
			.. "/src/viewporter-client-protocol.h",

		-- Relative Pointer
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/relative-pointer-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/relative-pointer-unstable-v1-client-protocol.h",
		-- Pointer Constraints
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/pointer-constraints-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/pointer-constraints-unstable-v1-client-protocol.h",

		-- Fractional Scale
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/fractional-scale-v1.xml ../"
			.. raylib_dir
			.. "/src/fractional-scale-v1-client-protocol.h",

		-- XDG Activation
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/xdg-activation-v1.xml ../"
			.. raylib_dir
			.. "/src/xdg-activation-v1-client-protocol.h",
		-- Idle Inhibit
		"@wayland-scanner client-header ../"
			.. raylib_dir
			.. "/src/external/glfw/deps/wayland/idle-inhibit-unstable-v1.xml ../"
			.. raylib_dir
			.. "/src/idle-inhibit-unstable-v1-client-protocol.h",
	})
	filter({})

	filter("action:vs*")
	defines({ "_WINSOCK_DEPRECATED_NO_WARNINGS", "_CRT_SECURE_NO_WARNINGS" })
	characterset("Unicode")
	buildoptions({ "/Zc:__cplusplus" })
	filter({})

	includedirs({ raylib_dir .. "/src", raylib_dir .. "/src/external/glfw/include" })
	vpaths({
		["Header Files"] = { raylib_dir .. "/src/**.h" },
		["Source Files/*"] = { raylib_dir .. "/src/**.c" },
	})
	files({ raylib_dir .. "/src/*.h", raylib_dir .. "/src/*.c" })

	removefiles({ raylib_dir .. "/src/rcore_*.c" })

	filter({ "system:macosx" })
	toolset("clang")
	buildoptions({ "-x objective-c" })
	links({
		"OpenGL.framework",
		"Cocoa.framework",
		"IOKit.framework",
		"CoreFoundation.framework",
		"CoreAudio.framework",
		"CoreVideo.framework",
		"AudioToolbox.framework",
	})

	filter({ "system:windows", "action:gmake*" })
	buildoptions({ "-Winvalid-pch" })

	filter({})
end
