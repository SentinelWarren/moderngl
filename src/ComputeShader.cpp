#include "Types.hpp"

#include "InlineMethods.hpp"

PyObject * MGLContext_compute_shader(MGLContext * self, PyObject * args) {
	PyObject * source;

	int args_ok = PyArg_ParseTuple(
		args,
		"O",
		&source
	);

	if (!args_ok) {
		return 0;
	}

	if (!PyUnicode_Check(source)) {
		MGLError_Set("the source must be a string not %s", Py_TYPE(source)->tp_name);
		return 0;
	}

	const char * source_str = PyUnicode_AsUTF8(source);

	MGLComputeShader * compute_shader = PyObject_New(MGLComputeShader, MGLComputeShader_type);

	Py_INCREF(self);
	compute_shader->context = self;

	const GLMethods & gl = self->gl;

	int program_obj = gl.CreateProgram();

	if (!program_obj) {
		MGLError_Set("cannot create program");
		return 0;
	}

	int shader_obj = gl.CreateShader(GL_COMPUTE_SHADER);

	if (!shader_obj) {
		MGLError_Set("cannot create the shader object");
		return 0;
	}

	gl.ShaderSource(shader_obj, 1, &source_str, 0);
	gl.CompileShader(shader_obj);

	int compiled = GL_FALSE;
	gl.GetShaderiv(shader_obj, GL_COMPILE_STATUS, &compiled);

	if (!compiled) {
		const char * message = "GLSL Compiler failed";
		const char * title = "ComputeShader";
		const char * underline = "=============";

		int log_len = 0;
		gl.GetShaderiv(shader_obj, GL_INFO_LOG_LENGTH, &log_len);

		char * log = new char[log_len];
		gl.GetShaderInfoLog(shader_obj, log_len, &log_len, log);

		gl.DeleteShader(shader_obj);

		MGLError_Set("%s\n\n%s\n%s\n%s\n", message, title, underline, log);

		delete[] log;
		return 0;
	}

	gl.AttachShader(program_obj, shader_obj);
	gl.LinkProgram(program_obj);

	int linked = GL_FALSE;
	gl.GetProgramiv(program_obj, GL_LINK_STATUS, &linked);

	if (!linked) {
		const char * message = "GLSL Linker failed";
		const char * title = "ComputeShader";
		const char * underline = "=============";

		int log_len = 0;
		gl.GetProgramiv(program_obj, GL_INFO_LOG_LENGTH, &log_len);

		char * log = new char[log_len];
		gl.GetProgramInfoLog(program_obj, log_len, &log_len, log);

		gl.DeleteProgram(program_obj);

		MGLError_Set("%s\n\n%s\n%s\n%s\n", message, title, underline, log);

		delete[] log;
		return 0;
	}

	compute_shader->shader_obj = shader_obj;
	compute_shader->program_obj = program_obj;

	int num_uniforms = 0;
	int num_uniform_blocks = 0;

	gl.GetProgramiv(program_obj, GL_ACTIVE_UNIFORMS, &num_uniforms);
	gl.GetProgramiv(program_obj, GL_ACTIVE_UNIFORM_BLOCKS, &num_uniform_blocks);

	PyObject * uniforms_lst = PyTuple_New(num_uniforms);
	PyObject * uniform_blocks_lst = PyTuple_New(num_uniform_blocks);

	int uniform_counter = 0;
	for (int i = 0; i < num_uniforms; ++i) {
		int type = 0;
		int array_length = 0;
		int name_len = 0;
		char name[256];

		gl.GetActiveUniform(program_obj, i, 256, &name_len, &array_length, (GLenum *)&type, name);
		int location = gl.GetUniformLocation(program_obj, name);

		clean_glsl_name(name, name_len);

		if (location < 0) {
			continue;
		}

		MGLUniform * mglo = (MGLUniform *)MGLUniform_Type.tp_alloc(&MGLUniform_Type, 0);
		mglo->type = type;
		mglo->location = location;
		mglo->array_length = array_length;
		mglo->program_obj = program_obj;
		MGLUniform_Complete(mglo, gl);

		PyObject * item = PyTuple_New(5);
		PyTuple_SET_ITEM(item, 0, (PyObject *)mglo);
		PyTuple_SET_ITEM(item, 1, PyLong_FromLong(location));
		PyTuple_SET_ITEM(item, 2, PyLong_FromLong(array_length));
		PyTuple_SET_ITEM(item, 3, PyLong_FromLong(mglo->dimension));
		PyTuple_SET_ITEM(item, 4, PyUnicode_FromStringAndSize(name, name_len));

		PyTuple_SET_ITEM(uniforms_lst, uniform_counter, item);
		++uniform_counter;
	}

	if (uniform_counter != num_uniforms) {
		_PyTuple_Resize(&uniforms_lst, uniform_counter);
	}

	for (int i = 0; i < num_uniform_blocks; ++i) {
		int size = 0;
		int name_len = 0;
		char name[256];

		gl.GetActiveUniformBlockName(program_obj, i, 256, &name_len, name);
		int index = gl.GetUniformBlockIndex(program_obj, name);
		gl.GetActiveUniformBlockiv(program_obj, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);

		clean_glsl_name(name, name_len);

		MGLUniformBlock * mglo = (MGLUniformBlock *)MGLUniformBlock_Type.tp_alloc(&MGLUniformBlock_Type, 0);

		mglo->index = index;
		mglo->size = size;
		mglo->program_obj = program_obj;
		mglo->gl = &gl;

		PyObject * item = PyTuple_New(4);
		PyTuple_SET_ITEM(item, 0, (PyObject *)mglo);
		PyTuple_SET_ITEM(item, 1, PyLong_FromLong(index));
		PyTuple_SET_ITEM(item, 2, PyLong_FromLong(size));
		PyTuple_SET_ITEM(item, 3, PyUnicode_FromStringAndSize(name, name_len));

		PyTuple_SET_ITEM(uniform_blocks_lst, i, item);
	}

	PyObject * result = PyTuple_New(4);
	PyTuple_SET_ITEM(result, 0, (PyObject *)compute_shader);
	PyTuple_SET_ITEM(result, 1, uniforms_lst);
	PyTuple_SET_ITEM(result, 2, uniform_blocks_lst);
	PyTuple_SET_ITEM(result, 3, PyLong_FromLong(compute_shader->program_obj));
	return result;
}

PyObject * MGLComputeShader_run(MGLComputeShader * self, PyObject * args) {
	unsigned x;
	unsigned y;
	unsigned z;

	int args_ok = PyArg_ParseTuple(
		args,
		"III",
		&x,
		&y,
		&z
	);

	if (!args_ok) {
		return 0;
	}

	const GLMethods & gl = self->context->gl;

	gl.UseProgram(self->program_obj);
	gl.DispatchCompute(x, y, z);

	Py_RETURN_NONE;
}

PyMethodDef MGLComputeShader_tp_methods[] = {
	{"run", (PyCFunction)MGLComputeShader_run, METH_VARARGS, 0},
	{0},
};

PyTypeObject * MGLComputeShader_type;

PyType_Slot MGLComputeShader_slots[] = {
	{Py_tp_methods, MGLComputeShader_tp_methods},
	{0},
};

PyType_Spec MGLComputeShader_spec = {"MGLComputeShader", sizeof(MGLComputeShader), 0, Py_TPFLAGS_DEFAULT, MGLComputeShader_slots};
