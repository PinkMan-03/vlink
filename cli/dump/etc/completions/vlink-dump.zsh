#compdef vlink-dump dump

# VLink zsh completion for vlink-dump.

_vlink-dump() {
    _arguments -s \
        '(-t --type)'{-t,--type}'=[Output format]:format:(console text csv json bin jpg jpeg h264 h265 raw pcd)' \
        '(-c --condition)'{-c,--condition}'=[Field list, comma-separated]:fields:' \
        '(-o --out_dir)'{-o,--out_dir}'=[Output directory]:dir:_files -/' \
        '(-m --base_name)'{-m,--base_name}'=[Output base name]:name:' \
        '(-f --bag_file --native)'{-f,--bag_file}'=[Bag source]:bag:_vlink_zsh_complete_play_file' \
        '(-b --begin_time --native)'{-b,--begin_time}'=[Bag begin time (s, requires -f)]:time:' \
        '(-e --end_time --native)'{-e,--end_time}'=[Bag end time (s, requires -f)]:time:' \
        '(-n --count)'{-n,--count}'=[Max count]:count:' \
        '--hz=[Max frequency]:hz:' \
        '(-f --bag_file -b --begin_time -e --end_time)--native[Native mode (mutually exclusive with -f/-b/-e)]' \
        '(-d --proto_dir)'{-d,--proto_dir}'=[Proto dir]:dir:_files -/' \
        '--fbs_dir=[FBS dir]:dir:_files -/' \
        '(-q --quiet)'{-q,--quiet}'[Quiet mode]' \
        '(-l --detail)'{-l,--detail}'[Detail mode]' \
        '(-x --expression)'{-x,--expression}'=[Math expression]:expr:' \
        '(-h --help)'{-h,--help}'[Show help]' \
        '(-v --version)'{-v,--version}'[Show version]' \
        ':url:_vlink_zsh_complete_url'
}

if (( $+functions[compdef] )); then
    compdef _vlink-dump vlink-dump dump
fi
