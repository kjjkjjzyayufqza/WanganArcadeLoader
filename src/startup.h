#pragma once

#include <toml.h>

void ConfigureStartup (toml_table_t *config, bool offline);
void InstallStartupHooks ();
