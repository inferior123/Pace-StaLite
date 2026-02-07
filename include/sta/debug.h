
using namespace sta;
void fanout_debuger(sta::STAWorker &worker);
void run_debuger(sta::STAWorker &worker, bool verbose);
void compare_bfs_dfs(sta::STAWorker &worker_bfs, sta::STAWorker &worker_dfs);

int test_celllib_parser(int argc, char *argv[]);
int test_transition_calculation(int argc, char *argv[]);

int test_all_sta_functions(int argc, char *argv[]);
int setup_hold_test(int argc, char *argv[]);

void gcd_test();
void auto_test(int /*argc*/, char * /*argv*/[]);
