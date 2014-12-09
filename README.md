Name:squid_videocahe
Author:cqkxboy168,cqkxboy168@qq.com
Desc:video cache for squid-2.7.STABLE9
.
├── etc                   squid配置文件
├── libvideoreg           视频缓存动态链接库，用来解析不同视频网站URL
├── README.md             说明文件
├── squid                 squid-2.7.STABLE9的patch的文件来支持视频缓存
└── squid-2.7.STABLE9-new 打过patch后的squid-2.7.STABLE9
Theory:
1.目前支持youku,tudou,letv等网站视频缓存。
2.由于视频网站url的规则可能变化，所以通过制作.so动态链接库的方式来解析视频地址URL,提出去视频文件的ID,squid默认使用url计算hash，本项目通过视频文件ID来计算hash，libvideoreg提供一些列视频地址解析函数的接口。
3.为了提高匹配效率，使用了多模式匹配算法来匹配视频URL。
1）通过维护2个DFA(有限状态自动机)来匹配keyword.txt和exclusions.txt里面的关键字。然后通过AC算法对URL进行关键字匹配，如果URL中有exclusions.txt的关键字，则URL不做处理。如果URL中有keyword.txt里的关键字，则squid会调用libvideoreg里对应的函数来解析视频URL.