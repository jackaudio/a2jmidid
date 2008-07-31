default:
	echo "use ./waf instead"

SOURCES = a2jmidid.c.* jack.c.* port.c.* port_thread.c.* port_hash.c.* 
#SOURCES += dbus.c.* dbus_iface_control.c.* dbus_iface_introspectable.c.*
#SOURCES += paths.c.*
#SOURCES += log.c.*

OMITS = --omit INIT_LIST_HEAD,list_empty,list_del,list_add_tail

# rebuild a2jmidid_call_graph.png
cg:
	cd build && egypt $(OMITS) $(SOURCES) | dot -Tpng /dev/stdin > call_graph.png

cgf:
	cd build && egypt --include-external $(SOURCES) | dot -Tpng /dev/stdin > call_graph.png
