# DROP TABLE 验证脚本

目标为 questionId=200001 下的 drop-table 子题，子题编号 400002。

## 已可在当前 Windows 环境运行的模块测试

在 miniob 目录执行：

```text
python lab_tests/run_unit.py
```

需要 g++ 支持 C++20。脚本直接包含生产代码 drop_table_files.h，不复制实现、不用 SQLite 替代 MiniOB。Windows 上执行 15 个资源管理测试；Linux 上额外编译官方 meta_util.cpp，校验文件名规则，共 16 个测试。测试输出保存到 lab_results/unit-test.log。

## Linux 完整编译与真实 SQL 测试

先按官方文档安装 GCC 11+、CMake、make、flex、bison，然后在有 .git 的 miniob 目录执行：

```bash
git submodule update --init --recursive
bash build.sh init
bash build.sh debug -DENABLE_ASAN=OFF -DWITH_UNIT_TESTS=OFF -DWITH_CPPLINGS=OFF --make -j2
python3 lab_tests/run_unit.py
python3 lab_tests/run_sql.py --observer build_debug/bin/observer --config etc/observer.ini --output /tmp/miniob-lab
```

run_sql.py 只启动自己的 observer，使用新建的独立目录及 Unix socket；不连接已有数据库。开启磁盘日志和 heap 引擎，测试删表前查询、双索引清理、重复删除、同名重建、保护其他表、显式事务拒绝、正常重启后的状态。日志与数据目录保留在脚本打印的位置。只有脚本实际返回成功，才能记录为 SQL 验证通过。

Linux 的实际运行记录见仓库 Actions 中与提交号对应的 `DROP TABLE Linux verification`。只有整个流程成功时才能记录整库验证通过；单个步骤成功不等于整个流程成功。OceanBase 官网成绩仍需独立提测。

CI 必须完整拉取子模块历史（checkout 的 fetch-depth: 0），因为官方 build.sh 会切换到指定 libevent 提交和 jsoncpp 标签。若手动使用浅克隆，需先补齐相应依赖历史。
