// Headless mGBA runner for the Soullike link end-to-end test.
//
// Runs a ROM with no window, loads Lua scripts the same way mGBA's
// Tools > Scripting window does, and fires the "frame" callback after
// every frame. Built against libmgba 0.10 with scripting enabled; see
// tools/soullike_link/README.md.
//
// Usage: harness ROM FRAMES SCRIPT.lua [SCRIPT.lua ...]

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/scripting.h>
#include <mgba/internal/script/lua.h>
#include <mgba/script/context.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void logToStdout(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger;
	// Only show script output and real problems, not the emulator's debug chatter.
	const char* id = mLogCategoryId(category);
	bool fromScript = id && strcmp(id, "script") == 0;
	if (fromScript || (level & (mLOG_FATAL | mLOG_ERROR))) {
		vprintf(format, args);
		printf("\n");
		fflush(stdout);
	}
}

int main(int argc, char** argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: %s ROM FRAMES SCRIPT.lua [SCRIPT.lua ...]\n", argv[0]);
		return 2;
	}
	long frames = strtol(argv[2], NULL, 10);

	struct mLogger logger = { .log = logToStdout };
	mLogSetDefaultLogger(&logger);

	struct mCore* core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) {
		fprintf(stderr, "could not load core for %s\n", argv[1]);
		return 1;
	}
	mCoreInitConfig(core, NULL);
	static color_t video[256 * 256];
	core->setVideoBuffer(core, video, 256);
	if (!mCoreLoadFile(core, argv[1])) {
		fprintf(stderr, "could not load %s\n", argv[1]);
		return 1;
	}
	core->reset(core);

	struct mScriptContext context;
	mScriptContextInit(&context);
	mScriptContextAttachStdlib(&context);
	mScriptContextAttachSocket(&context);
	struct mScriptEngineContext* lua = mScriptContextRegisterEngine(&context, mSCRIPT_ENGINE_LUA);
	mScriptContextAttachCore(&context, core);
	mScriptContextAttachLogger(&context, &logger);

	// Same as loading a script from mGBA's scripting window: load, then run it.
	for (int i = 3; i < argc; ++i) {
		struct VFile* vf = VFileOpen(argv[i], O_RDONLY);
		if (!vf || !lua->load(lua, argv[i], vf) || !lua->run(lua)) {
			fprintf(stderr, "could not run script %s: %s\n", argv[i], lua->getError ? lua->getError(lua) : "");
			return 1;
		}
		vf->close(vf);
	}

	mScriptContextTriggerCallback(&context, "start");
	for (long f = 0; f < frames; ++f) {
		core->runFrame(core);
		mScriptContextTriggerCallback(&context, "frame");
		// Roughly real-time, so the two emulators run at a similar pace.
		usleep(16000);
	}
	mScriptContextTriggerCallback(&context, "stop");

	mScriptContextDetachCore(&context);
	mScriptContextDeinit(&context);
	core->deinit(core);
	return 0;
}
