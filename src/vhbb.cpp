#include "vhbb.h"

#include "Views/CategoryView/categoryView.h"
#include "Views/background.h"
#include "Views/statusBar.h"
#include "activity.h"
#include "database.h"
#include "input.h"
#include "network.h"
#include "nosleep_thread.h"
#include "splash_thread.h"

extern "C" {
unsigned int sleep(unsigned int seconds) {
  sceKernelDelayThread(seconds * 1000 * 1000);
  return 0;
}

int usleep(useconds_t usec) {
  sceKernelDelayThread(usec);
  return 0;
}

void __sinit(struct _reent *);
}

__attribute__((constructor(101))) void pthread_setup(void) {
  pthread_init();
  __sinit(_REENT);
}

void terminate_logger() {
  std::exception_ptr p = std::current_exception();
  try {
    std::rethrow_exception(p);
  } catch (const std::exception &e) {
    dbg_printf(DBG_ERROR, "terminate() because of %s", e.what());
  }
}

int main() {
  sceIoMkdir(VHBB_DATA.c_str(), 0777);

  vita2d_init();
  vita2d_set_clear_color(COLOR_BLACK);

  // Sleep crashes the app
  SceUID thid_sleep = sceKernelCreateThread(
      "nosleep_thread", (SceKernelThreadEntry)nosleep_thread, 0x40, 0x1000, 0,
      0, NULL);
  sceKernelStartThread(thid_sleep, 0, NULL);

  // displaySplash();
  SceUID thid_spl = sceKernelCreateThread("splash_thread",
                                          (SceKernelThreadEntry)splash_thread,
                                          0x40, 0x10000, 0, 0, NULL);
  sceKernelStartThread(thid_spl, 0, NULL);

  dbg_init();

  std::set_terminate(terminate_logger);

  Network &network = *Network::create_instance();

  // FIXME Don't crash if network not available, see
  // https://bitbucket.org/xerpi/vita-ftploader/src/87ef1d13a8aaf092f376cbf2818a22cd0e481fd6/plugin/main.c?at=master&fileviewer=file-view-default#main.c-155

  try {
    // TODO check if fails
    network.Download(API_ENDPOINT, API_LOCAL);
    auto db = Database::create_instance(API_LOCAL);
    dbg_printf(DBG_DEBUG, "Instance created");
    db->DownloadIcons();
  } catch (const std::exception &ex) {
    dbg_printf(DBG_ERROR, "Couldn't load database: %s", ex.what());
    throw ex;
  }

  Input input;

  Activity &activity = *Activity::create_instance();

  Background background;

  StatusBar statusBar;
  CategoryView categoryView;

  while (1) {
    // sceKernelPowerTick(0);
    vita2d_start_drawing();
    vita2d_clear_screen();

    input.Get();
    // input.Propagate(curView); // TODO: Rework function
    activity.FlushQueue();
    if (!activity.HasActivity()) {
      categoryView.HandleInput(1, input);
    } else {
      categoryView.HandleInput(0, input);
    }

    activity.HandleInput(1, input);

    background.Display();
    if (!activity.HasActivity()) categoryView.Display();

    statusBar.Display();

    activity.Display();

    vita2d_end_drawing();
    vita2d_swap_buffers();
    sceDisplayWaitVblankStart();
  }

  return 0;
}
