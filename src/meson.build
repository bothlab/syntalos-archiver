
sya_hdr = [
    'sqfs-simple-writer.h',
]

sya_src = [
    'sqfs-simple-writer.c',
    'sqfs-fstree.c',
]

sya_moc_src = [
    'main.cpp'
]

sya_ui = [

]

sya_moc = qt.preprocess(
    moc_headers: sya_hdr,
    moc_sources: sya_moc_src,
    ui_files: sya_ui,
    moc_extra_arguments: ['--no-notes']
)

sya_exe = executable('syntalos-archiver',
    [sya_src, sya_moc_src, sya_moc, config_h],
    gnu_symbol_visibility: 'hidden',
    dependencies: [qt_core_dep,
                   qt_gui_dep,
                   qt_svg_dep,
                   sqfs_dep,
                ],
    include_directories: [root_include_dir],
    install: true,
)
