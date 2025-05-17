# Codeforces API*

## 必要的API

1.   [codeforces.com/api/user.rating?handle=Nexus_Raphael](https://codeforces.com/api/user.rating?handle=Nexus_Raphael)
     1.   获取单个用户的rating变化记录
     2.   如果是多个用户，目前不能够直接通过用户名之间加分号实现
2.   https://codeforces.com/api/user.info?handles=DmitriyH;Fefer_Ivan&checkHistoricHandles=false

     1.   获取一个以上的用户信息
     2.   checkHistoryHandles是查历史上使用过这个名字的用户，可能没有用（或者是我理解错误）
3.   [codeforces.com/api/contest.ratingChanges?contestId=2102](https://codeforces.com/api/contest.ratingChanges?contestId=2102)
     1.   这个是查找某场比赛所有参赛成员的rating变化，但是怎么筛指定人员还想不出，可能要在代码中实现



## 可能用得上的API

1.   [codeforces.com/api/user.status?handle=Nexus_Raphael&from=1&count=12](https://codeforces.com/api/user.status?handle=Nexus_Raphael&from=1&count=12)
     1.   查找某个人最近的提交记录
     2.   from是选取从最近（1）开始计算count次的记录，超过次数之后也返回OK，结果为所有次数
2.   [codeforces.com/api/problemset.problems?tags=implementation](https://codeforces.com/api/problemset.problems?tags=implementation)
     -   通过标签筛题，但是不知道怎么筛rating段和正反序
     -   现在这样找是从最近的题开始，没有难度顺序
