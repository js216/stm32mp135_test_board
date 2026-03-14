" Plugins
set nocompatible
filetype off
filetype plugin indent on

" Misc options
set number
set shiftwidth=3
set ignorecase
set autoindent
set incsearch
set lbr
set timeoutlen=500
set hidden
set splitbelow
set splitright
set expandtab
set visualbell
set belloff=all
set autochdir
set showmode
set showcmd
set ruler
set laststatus=1
set hlsearch
set mouse=nvi
set formatoptions+=t
set textwidth=80
set nowrap
set undofile
set undodir=$HOME/.vim/undo
set backupdir=~/.vimtmp//,.
set directory=~/.vimtmp//,.
set undolevels=10000
set undoreload=100000

" Misc Mappings
noremap k gk
noremap j gj
noremap Q <Nop>
noremap K <C-w><C-w>
noremap <C-P> <C-B>
noremap <Tab> :bn<CR>
noremap <S-Tab> :bp<CR>
command Wall wall
map _ :Hexplore<CR>
map + :Explore<CR>
map \ :Texplore<CR>
map <BS> :vsplit<CR>:Explore<CR>

" Leader commands
let mapleader = " "
map <leader><Space> :noh<CR>
map <leader>h <C-w>h
map <leader>l <C-w>l
map <leader>k <C-w>k
map <leader>j <C-w>j
map <leader>c :cd ..<CR>:pw<CR>
map <leader>f :Ack!<space>
map <leader>g :let @/=expand("<cword>")<Bar>wincmd w<Bar>normal n<CR>
map <leader>1 <Esc>:tabn 1<CR>
map <leader>2 <Esc>:tabn 2<CR>
map <leader>3 <Esc>:tabn 3<CR>
map <leader>4 <Esc>:tabn 4<CR>
map <leader>5 <Esc>:tabn 5<CR>
map <leader>6 <Esc>:tabn 6<CR>
map <leader>7 <Esc>:tabn 7<CR>
map <leader>8 <Esc>:tabn 8<CR>
map <leader>9 <Esc>:tablast<CR>
map <leader>n <Esc>:tabn<CR>
map <leader>p <Esc>:tabp<CR>
map <leader>d <Esc>:bd<CR>
map <leader>q <Esc>:q<CR>

" Function keys
map <F1> <Esc>:tabp<CR>
map <F2> <Esc>:tabn<CR>
map <F3> <Esc>:windo diffthis<CR>
map <F4> <Esc>:windo diffoff<CR>
map <F5> @a
map <F6> :set paste<CR>i
map <F7> <Esc>:set nopaste<CR>
map <F8> <Esc>:!make<CR>
map <F9> :vertical resize -20<CR>
map <F10> :resize -10<CR>
map <F11> :resize +10<CR>
map <F12> :vertical resize +20<CR>
map <S-F9> :vertical resize -2<CR>
map <S-F10> :resize -1<CR>
map <S-F11> :resize +1<CR>
map <S-F12> :vertical resize +2<CR>
inoremap <F8> TODO: remove this
inoremap <F12> <C-R>=strftime("%m/%d/%Y")<CR>

" Clipboard
if has('win32') || has('win64')
   " Windows
   vnoremap Y y:call system('clip.exe', @")<CR>
elseif has('unix')
   if filereadable('/proc/version') &&
            \ matchstr(join(readfile('/proc/version')), 'Microsoft') != ''
      " WSL2
      vnoremap Y y:call system('/mnt/c/Windows/System32/clip.exe', @")<CR>
   else
      " Linux
      vnoremap Y y:call system('xclip -selection clipboard', @")<CR>
   endif
endif

" Files based on filename
au BufNewFile,BufRead Kconfig,Kconfig.debug,*.in setf kconfig
au BufRead,BufNewFile *.jlinkscript set filetype=c

" Open file read-only if it already has a swapfile
autocmd SwapExists * let v:swapchoice = "o"

" New commands
command LinuxStyle :set autoindent noexpandtab tabstop=8 shiftwidth=8
command! E execute 'let v=winsaveview() | edit | call winrestview(v)'

" Appearance
let g:netrw_banner = 0

" Visible tabs and trailing spaces
set list listchars=tab:\|-
highlight WhitespaceEOL ctermbg=red guibg=red
autocmd FileType * match WhitespaceEOL /\s\+$/
autocmd BufEnter * highlight OverLength ctermbg=blue guibg=#111111
autocmd BufEnter * match OverLength /\%82v.*/

" Custom syntax
autocmd BufNewFile,BufRead *.nw set syntax=noweb

" Tab line formatter function
function! Tabline() abort
   let l:line = ''
   let l:current = tabpagenr()
   for l:i in range(1, tabpagenr('$'))
      " Distinguish between current and other tabs
      if l:i == l:current
         let l:line .= '%#TabLineSel#'
      else
         let l:line .= '%#TabLine#'
      endif
      " Put filename in the tab label
      let l:label = fnamemodify(
               \ bufname(tabpagebuflist(l:i)[tabpagewinnr(l:i) - 1]),
               \ ':t'
               \ )
      " Add '[+]' if one of the buffers in the tab page is modified
      let bufnrlist = tabpagebuflist(l:i)
      for bufnr in bufnrlist
         if getbufvar(bufnr, "&modified")
            let l:label .= '[+]'
            break
         endif
      endfor
      " Prefix tab number
      let l:label = printf('%d: %s', l:i, l:label)
      " Assemble tab line from tab labels
      let l:line .= '%' .  i .  'T' " Starts mouse click target region.
      let l:line .= ' ' .  l:label .  ' '
   endfor
   " Finish the tab line
   let l:line .= '%#TabLineFill#'
   let l:line .= '%T' " Ends mouse click target region(s).
   return l:line
endfunction

" Format tab line
set tabline=%!Tabline()
highlight TabLine cterm=none
highlight TabLine ctermbg=lightgrey ctermfg=black
highlight TabLineSel ctermbg=blue

" Show total line count of file in status bar
set statusline =%1*\ %n\ %*     "buffer number
set statusline +=%5*%{&ff}%*    "file format
set statusline +=%3*%y%*        "file type
set statusline +=%4*\ %<%F%*    "full path
set statusline +=%2*%m%*        "modified flag
set statusline +=%1*%=%5l%*     "current line
set statusline +=%2*/%L%*       "total lines
set statusline +=%1*%4v\ %*     "virtual column number
set statusline +=%2*0x%04B\ %*  "character under cursor

" Colors for statusline (terminal mode)
hi StatusLineNC cterm=NONE
hi User1 ctermfg=black ctermbg=cyan
hi User2 ctermfg=black ctermbg=cyan
hi User3 ctermfg=black ctermbg=cyan
hi User4 ctermfg=black ctermbg=cyan
hi User5 ctermfg=black ctermbg=cyan

" Colors for statusline (GUI mode)
hi User1 guifg=#eea040 guibg=#444444
hi User2 guifg=#dd3333 guibg=#444444
hi User3 guifg=#ff66ff guibg=#444444
hi User4 guifg=#a0ee40 guibg=#444444
hi User5 guifg=#eeee40 guibg=#444444

" Gui options
set guioptions-=e  "tabline matches overall colorscheme
set guioptions-=m  "remove menu bar
set guioptions-=T  "remove toolbar
set guioptions-=r  "remove right-hand scroll bar
set guioptions-=L  "remove left-hand scroll bar
set guitablabel=%N:%M%t "tabs display filename, not complete path
set guifont=Consolas:h10.8:cANSI
au GUIEnter * simalt ~x

" Set block cursor
let &t_ti.="\e[1 q"
let &t_SI.="\e[5 q"
let &t_EI.="\e[1 q"
let &t_te.="\e[0 q"
