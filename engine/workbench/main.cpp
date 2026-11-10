#include <paperboat/sdk.hpp>

#include "workbench.hpp"

int main(int argc, char** argv) {
  // 8765 is the engine's default backend port, so the web app's default URL points here.
  return paperboat::core::main(paperboat::workbench::backend(), argc, argv, 8765);
}
