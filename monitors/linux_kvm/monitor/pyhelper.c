#include <stdio.h>
#include <err.h>
#include <python2.6/Python.h>
#include "pyhelper.h"


static struct uvmem_server_arg serve_uvmem_page_sarg;

/* Return the number of arguments of the application command line */
static PyObject* emb_get_sarg(PyObject *self, PyObject *args)
{
    if(!PyArg_ParseTuple(args, ":get_sarg"))
        return NULL;
    int page_fd = serve_uvmem_page_sarg.fp ? fileno(serve_uvmem_page_sarg.fp) : -1,
        uvmem_fd = serve_uvmem_page_sarg.uvmem_fd,
        shmem_fd = serve_uvmem_page_sarg.shmem_fd,
        pico_id = serve_uvmem_page_sarg.pico_id;
    unsigned long map_size = serve_uvmem_page_sarg.size,
        page_size = serve_uvmem_page_sarg.page_size;
    return Py_BuildValue("iiikki", page_fd, uvmem_fd, shmem_fd, map_size, page_size, pico_id);
}

static PyObject* emb_find_page_file_offset(PyObject *self, PyObject *args)
{
	unsigned long vaddr;
	long offset;
	if(!PyArg_ParseTuple(args, "k:find_page_file_offset", &vaddr))
        return NULL;
    offset = find_page_file_offset((uint8_t*)vaddr);
    return Py_BuildValue("l", offset);
}

static PyMethodDef EmbMethods[] = {
    {"get_sarg", emb_get_sarg, METH_VARARGS,
     "Return the current uvmem_server_arg as a tuple."},
    {"find_page_file_offset", emb_find_page_file_offset, METH_VARARGS,
     "Return page file offset of given virtual address."},
    {NULL, NULL, 0, NULL}
};

void py_serve_uvmem_page(struct uvmem_server_arg *sarg)
{
	if(sarg == NULL)
		err(EXIT_FAILURE, "py_serve_uvmem_page cannot init with NULL.");

	serve_uvmem_page_sarg = *sarg;
	Py_Initialize();
	Py_InitModule("emb", EmbMethods);
	const char *filename = "/elasticity/embassies/monitors/linux_kvm/scripts/kvm_monitor_uvmem_helper.py";
	FILE *fp = fopen(filename, "r");
	assert(fp != NULL);
	PyRun_SimpleFile(fp, "kvm_monitor_uvmem_helper.py");
	Py_Finalize();
}
