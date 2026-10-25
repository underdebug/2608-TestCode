"""Run with F8 while CUDA-GDB is paused after fChannelPos is assigned."""

# F8 supplies gpu(); scalar kernel locals are read directly.
fChannelPos = mem('fChannelPos', 'f')

print(fChannelPos)
