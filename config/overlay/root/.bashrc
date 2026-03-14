set -o vi
export EDITOR=vim
bind -x '"\C-l": clear'

# history
shopt -s histappend
PROMPT_COMMAND='history -a'
export HISTSIZE=
export HISTFILESIZE=
export HISTFILE=~/.bash_eternal_history
export HISTTIMEFORMAT="[%F %T] "

# settings aliases
alias ls="ls --color"
alias watch="watch --color"
alias grep="grep --color"
alias less="less -R"
alias feh="feh --scale-down"
alias vi="busybox vi"

# abbreviating aliases
alias ll="ls -lht"
alias ga="git add . && git status"
alias gc="git commit"
alias gs="git status"
