set terminal postscript eps enhanced color font 'Helvetica,10'

set logscale x
set xrange [0.1:]
set xlabel "File size (KB)"
set ylabel "Time to download (ms)"
set boxwidth 0.2
plot "fetch.result.0.txt" using ($1/1024):2:3:4:5 with candlesticks whiskerbars 0.5 lw 2, \
'' using ($1/1024):6:6:6:6 with candlesticks notitle lw 2 lc rgb "#008800"


