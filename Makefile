default:
	echo "use ./waf instead"

# rebuild a2jmidid_call_graph.png
cg:
	cd build && egypt a2jmidid.c.* port.c.* port_thread.c.* | dot -Tpng /dev/stdin > call_graph.png
