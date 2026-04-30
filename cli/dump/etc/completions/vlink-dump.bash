# VLink bash completion for vlink-dump.

_vlink_dump() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD-1]}"

    COMPREPLY=()

    case "$prev" in
        -t|--type)
            _vlink_bash_complete_words "console text csv json bin jpg jpeg h264 h265 raw pcd" "$cur"
            return
            ;;
        -o|--out_dir|-d|--proto_dir|--fbs_dir)
            _vlink_bash_complete_dirs "$cur"
            return
            ;;
        -f|--bag_file)
            _vlink_bash_complete_files_ext "$cur" "$_vlink_bash_play_ext"
            return
            ;;
        -c|--condition|-m|--base_name|-b|--begin_time|-e|--end_time|-n|--count|--hz|-x|--expression)
            return
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        _vlink_bash_complete_words "-t --type -c --condition -o --out_dir -m --base_name \
-f --bag_file -b --begin_time -e --end_time -n --count --hz --native \
-d --proto_dir --fbs_dir -q --quiet -l --detail -x --expression \
-h --help -v --version" "$cur"
        return
    fi

    _vlink_bash_complete_url "$cur"
}

complete -F _vlink_dump vlink-dump dump
