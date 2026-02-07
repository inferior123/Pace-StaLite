#ifndef CELLLIB_CACHE_HPP
#define CELLLIB_CACHE_HPP

#include "cell_data_structure.hpp"
#include <string>
#include <vector>

namespace celllib {

/**
 * 若缓存有效则从缓存加载 CellLibrary，避免重复解析 .lib。
 * 缓存键为 lib 文件路径列表 + 各文件 mtime，缓存目录为 .sta_cache/。
 * @param lib_file_paths 要加载的 .lib 文件路径列表（与 SDC 中 link_library 一致）
 * @param out 成功时写入解析结果
 * @return 是否从缓存加载成功；若为 false 则需正常 parse 再调用 save_celllib_cache
 */
bool try_load_celllib_cache(const std::vector<std::string> &lib_file_paths,
                            CellLibrary &out);

/**
 * 将当前解析好的 CellLibrary 写入缓存，供下次 try_load_celllib_cache 使用。
 */
void save_celllib_cache(const std::vector<std::string> &lib_file_paths,
                        const CellLibrary &lib);

} // namespace celllib

#endif
