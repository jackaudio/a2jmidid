default:
	echo "use ./waf instead"

SOURCES = a2jmidid.c.* port.c.* port_thread.c.* port_hash.c.*

# rebuild a2jmidid_call_graph.png
cg:
	cd build && egypt $(SOURCES) | dot -Tpng /dev/stdin > call_graph.png

cgf:
	cd build && egypt --include-external $(SOURCES) | dot -Tpng /dev/stdin > call_graph.png
