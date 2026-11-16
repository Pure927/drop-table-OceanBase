# OceanBase 训练营 DROP TABLE 内核实验

本项目在 OceanBase 官方 MiniOB 源码上实现 DROP TABLE，属于训练营 `questionId=200001` 的 `drop-table` 子题，编号 `400002`。完整题目要求是删除表并清理其全部关联资源，包含后来创建的索引。本项目没有实现题目组中其余 17 个子题。

## 当前验证状态

- 2026-11-16，Windows / MinGW GCC 15.1.0：资源清理模块 15 项测试通过，0 项失败。
- Linux 整库编译与 SQL 回归：尚未执行。已提供可执行的测试脚本。
- OceanBase 官网提测：尚未提交，无平台通过记录和成绩。

模块通过不等于数据库整库通过，也不等于官网通过。实验报告中的这三类状态分别记录。

## 官方基线

- 上游地址：https://github.com/oceanbase/miniob
- 基线分支：main
- 基线提交：3259c37c8ddd8900ab045c8ce1fa1f5bf4d1e67d
- 题目组：https://open.oceanbase.com/train?questionId=200001
- 许可证：上游 Mulan PSL v2，保留 License、NOTICE 和原文件版权。

官网题目跨年保留，源码持续演进。本次使用固定的官方 main 提交补齐缺失功能；是否与 200001 的当前评测构建环境完全兼容，需要实际提测核实。

## 修改路径

1. 已有 yacc 语法将 `drop table t;` 生成 SCF_DROP_TABLE，不重复修改语法。
2. Stmt::create_stmt 分发给新增 DropTableStmt，检查表名和表是否存在。
3. CommandExecutor 分发给 DropTableExecutor，检查当前数据库并拒绝显式多语句事务。
4. Db::drop_table 从表元数据精确枚举 `.table`、`.data`、`.lob`、`.table.tmp` 和每个索引文件；不使用模糊前缀删除。
5. 删除前 Db::sync 刷新数据、double-write 页和日志检查点。补上原 sync 中 double-write 失败没有立即返回的问题。
6. DropTableFiles 先校验全部文件，再将它们改名为同目录 `.drop` 文件；改名失败尝试逆序回退。
7. 从 opened_tables_ 移除条目，释放 Table 以及记录、缓存池、索引和 LOB 句柄，然后删除 `.drop` 文件。失败返回 IOERR_WRITE。
8. 兼容旧 DefaultHandler::drop_table 调用入口。

## 使用边界

针对 Linux、heap 存储、默认无并发训练配置。不得存在其他活动事务或同时访问该表的请求；沿用 MiniOB Db 的外部串行化约束。显式 BEGIN 中拒绝 DROP，LSM 引擎返回 UNSUPPORTED。

该实现不提供崩溃原子的 DDL；文件改名后进程意外退出可能留下 `.drop` 文件。unlink 失败时表已从目录移除，返回失败并保留未清理文件，不能视为事务回滚。上游析构过程中的个别 close 错误不向调用者传播；需要 Linux 测试进一步验证文件句柄生命周期。以上边界不能用模块测试或正常重启测试消除。

## 编译与运行

参见 `lab_tests/README.md`。推荐 Linux 环境或官方在线开发环境。完整编译依赖第三方子模块；提交代码时保留 `.gitmodules` 和 Git 子模块指针，不上传 Windows 的测试可执行文件。

测试命令：

```bash
python3 lab_tests/run_unit.py
python3 lab_tests/run_sql.py --observer build_debug/bin/observer --config etc/observer.ini --output /tmp/miniob-lab
```

## 创建个人仓库与提测

请阅读提交包外层的《仓库与官网提测步骤.md》。创建空白 GitHub 或 Gitee 仓库后，将本地仓库推送到个人地址。平台使用的是仓库地址、分支和完整 commit id，不是课程提交 ZIP。

必须先运行 Linux 测试、修复失败并推送最终提交，再在训练营 200001 题目组提测。保存结果页、任务编号、时间和日志，在报告第 5 页补入真实结果。课程 5 分与训练营子题 10 分是两套评分，不可互换。
