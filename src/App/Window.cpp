#include "Window.h"

#include <QMouseEvent>
#include <QLabel>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QVBoxLayout>
#include <QScreen>

#include <array>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <QCheckBox>
#include <QSlider>
#include <tinygltf/tiny_gltf.h>

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

const QString MODEL_TO_LOAD = ":Models/Duck2.glb";


Window::Window() noexcept
{
	auto layout = new QVBoxLayout();
	setLayout(layout);

	/*  FPS  */
	const auto formatFPS = [](const auto value) {
		return QString("FPS: %1").arg(QString::number(value));
	};

	auto fps = new QLabel(formatFPS(0), this);
	fps->setStyleSheet("QLabel { color : white; }");

	connect(this, &Window::updateUI, [=] {
		fps->setText(formatFPS(ui_.fps));
	});

	layout->addWidget(fps, 1);


	/*  MORPH  */
	auto morphLabel = new QLabel("Morph:");
	auto morphSlider = new QSlider();
	morphSlider->setRange(0, 100);
	morphSlider->setSingleStep(1);
	morphSlider->setOrientation(Qt::Horizontal);

	layout->addWidget(morphLabel);
	layout->addWidget(morphSlider);

	connect(morphSlider, &QSlider::valueChanged, this, &Window::morph);

	/*  LIGHT  */
	auto diffuseCheckbox = new QCheckBox();
	diffuseCheckbox->setChecked(false);
	diffuseCheckbox->setText("Diffuse Light");
	auto spotCheckbox = new QCheckBox();
	spotCheckbox->setChecked(false);
	spotCheckbox->setText("Spot Light");

	layout->addWidget(diffuseCheckbox);
	layout->addWidget(spotCheckbox);

	connect(diffuseCheckbox, &QCheckBox::stateChanged, this, &Window::switchDiffuseLight);
	connect(spotCheckbox, &QCheckBox::stateChanged, this, &Window::switchSpotLight);

	/* CAMERA */
	auto freecamCheckbox = new QCheckBox();
	freecamCheckbox->setChecked(false);
	freecamCheckbox->setText("Free Camera");

	layout->addWidget(freecamCheckbox, 1);

	connect(freecamCheckbox, &QCheckBox::stateChanged, this, &Window::changeCameraType);

	/* CAMERA RELATIVE UP */
	auto relativeUpCheckbox = new QCheckBox();
	relativeUpCheckbox->setChecked(false);
	relativeUpCheckbox->setEnabled(false);
	relativeUpCheckbox->setText("Use relative up");

	layout->addWidget(relativeUpCheckbox, 2);

	connect(freecamCheckbox, &QCheckBox::stateChanged, relativeUpCheckbox, &QCheckBox::setEnabled);
	connect(relativeUpCheckbox, &QCheckBox::stateChanged, this, &Window::relativeUp);

	/* CAMERA STATS */
	cameraStats_ = new QLabel();
	cameraStats_->setText(currentCamera_->getStats());
	layout->addWidget(cameraStats_);

	/* misc */
	timer_.start();
}

Window::~Window()
{
	{
		// Free resources with context bounded.
		const auto guard = bindContext();
		program_.reset();
	}
}

void Window::onInit()
{
	// Configure shaders
	program_ = std::make_unique<QOpenGLShaderProgram>(this);
	program_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/Shaders/vertex.glsl");
	program_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/Shaders/fragment.glsl");
	program_->link();

	// Create VAO object
	vao_.create();
	vao_.bind();

	// Load and bind model
	gltfLoadModel();
	gltfBindModel();

	// Bind attributes
	program_->bind();

	uniforms_.mvp = program_->uniformLocation("mvp");
	uniforms_.model = program_->uniformLocation("modelMat");
	uniforms_.view = program_->uniformLocation("viewMat");
	uniforms_.normal = program_->uniformLocation("normalMat");
	uniforms_.morph = program_->uniformLocation("sphereMorph");
	uniforms_.enableDiffuse = program_->uniformLocation("enableDiffuse");
	uniforms_.enableSpot = program_->uniformLocation("enableSpot");

	// Release all
	program_->release();

	vao_.release();

	// Еnable depth test and face culling
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	// Clear all FBO buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

//	model_ = glm::rotate(glm::mat4(1), glm::radians(-45.f), glm::vec3(0, 1, 0));
	model_ = glm::mat4(1);
}

void Window::onRender()
{
	const auto guard = captureMetrics();

	// Clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Bind VAO and shader program
	program_->bind();
	vao_.bind();

	// Clear screen
	glClearColor(0.2, 0.2, 0.2, 1.0);		// background color
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Activate texture unit and bind texture
	glActiveTexture(GL_TEXTURE0);

	// Render
	render();

	cameraStats_->setText(currentCamera_->getStats());

	// Release VAO and shader program
	vao_.release();
	program_->release();

	++frameCount_;

	GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		qDebug() << "OpenGL Error: " << error;
	}

	// Request redraw if animated
	if (animated_)
	{
		update();
	}
}

void Window::onResize(const size_t width, const size_t height)
{
	// Configure viewport
	glViewport(0, 0, static_cast<GLint>(width), static_cast<GLint>(height));

	// Configure matrix
	const auto aspect = static_cast<float>(width) / static_cast<float>(height);
	const auto zNear = 0.1f;
	const auto zFar = 100.0f;
	const auto fov = 45.0f;
	projection_ = glm::perspective(glm::radians(fov), aspect, zNear, zFar);
}

void Window::mousePressEvent(QMouseEvent* got_event)
{
	mouseTrack_ = true;
	mouseTrackStart_ = got_event->pos();
}
void Window::mouseMoveEvent(QMouseEvent* got_event)
{
	if (!mouseTrack_) {
		return;
	}

	auto pos = got_event->pos();
	auto deltaX = mouseTrackStart_.x() - pos.x();
	auto deltaY = pos.y() - mouseTrackStart_.y(); // Inverted Y
	currentCamera_->updateRotation(static_cast<float>(deltaX), static_cast<float>(deltaY));
	mouseTrackStart_ = pos;
	update();

}
void Window::mouseReleaseEvent(QMouseEvent* /* ignore */)
{
	mouseTrack_ = false;
}

void Window::keyPressEvent(QKeyEvent * got_event) {
	auto key = (Qt::Key)got_event->key();

	static std::map<Qt::Key, glm::vec3> keymap = {
		{Qt::Key_W, {1, 0, 0}},
		{Qt::Key_S, {-1, 0, 0}},
		{Qt::Key_A, {0, 0, -1}},
		{Qt::Key_D, {0, 0, 1}},
		{Qt::Key_X, {0, 1, 0}},
		{Qt::Key_C, {0, -1, 0}},
	};

	if (!keymap.contains(key)) {
		qDebug() << "Map does not contain pressed key...." << key << got_event->text();
		return;
	}

	auto delta = keymap[key];
	currentCamera_->updatePosition(delta.x, delta.z, delta.y);

	update();
}

void Window::changeCameraType(bool free)
{
	if (free) {
		// Update eye_
		freeCamera_.eye_ = rotatingCamera_.eye_;

		// Update pitch_
		freeCamera_.pitch_ = std::fabs( rotatingCamera_.theta_) - 90;

		// Update yaw_
		float sign = rotatingCamera_.phi_ > 0 ? 1 : -1;
		freeCamera_.yaw_ = -sign * (180 - std::abs(rotatingCamera_.phi_));

		// misc
		freeCamera_.updateRotation(0, 0); // Force update front_ vec
		currentCamera_ = &freeCamera_;
	} else {
		auto eye = freeCamera_.eye_;

		auto len = glm::length(eye);
		auto xz_len = glm::length(glm::vec2(eye.x, eye.z));

		rotatingCamera_.radius_ = len;
		rotatingCamera_.theta_ = glm::degrees(glm::acos(eye.y / len));
		rotatingCamera_.phi_ = glm::degrees(glm::asin(eye.z / xz_len));

		rotatingCamera_.updatePosition(0, 0, 0);
		currentCamera_ = &rotatingCamera_;
	}

	qDebug() << "Free: pitch_" << freeCamera_.pitch_ << "yaw_" << freeCamera_.yaw_;
	qDebug() << "Rot : Theta" << rotatingCamera_.theta_ << "Phi" << rotatingCamera_.phi_;

	emit updateUI();
}
void Window::switchDiffuseLight(bool enable)
{
	enableDiffuse_ = enable;
}

void Window::switchSpotLight(bool enable)
{
	enableSpot_ = enable;
}
void Window::morph(int val)
{
	morph_ = static_cast<float>(val) / 100.f;
}
void Window::relativeUp(bool val)
{
	freeCamera_.relativeUp_ = val;
}

Window::PerfomanceMetricsGuard::PerfomanceMetricsGuard(std::function<void()> callback)
	: callback_{ std::move(callback) }
{}

Window::PerfomanceMetricsGuard::~PerfomanceMetricsGuard()
{
	if (callback_) {
		callback_();
	}
}

auto Window::captureMetrics() -> PerfomanceMetricsGuard
{
	return PerfomanceMetricsGuard{
		[&] {
			if (timer_.elapsed() >= 1000)
			{
				const auto elapsedSeconds = static_cast<float>(timer_.restart()) / 1000.0f;
				ui_.fps = static_cast<size_t>(std::round(frameCount_ / elapsedSeconds));
				frameCount_ = 0;
				emit updateUI();
			}
		}
	};
}

void Window::gltfLoadModel()
{
	auto ctx = tinygltf::TinyGLTF();

	std::string err;
	std::string warn;

	auto file = QFile(MODEL_TO_LOAD);

	if (!file.open(QIODevice::ReadOnly)) {
		qDebug() << "Cannot open file " << file.fileName();
		throw std::runtime_error("Cannot open model file");
	}

	auto qBytes = file.readAll();
	auto bytes = reinterpret_cast<const unsigned char*>(qBytes.constData());
	auto len = qBytes.length();

	bool res = ctx.LoadBinaryFromMemory(&gltfModel_, &err, &warn, bytes, len);

	if (!res) {
		qDebug() << "Couldn't load the model";
		qDebug() << "WARN: " << QString::fromStdString(warn);
		qDebug() << "ERR:  " << QString::fromStdString(err);
		throw std::runtime_error("Cannot load model");
	}

	qDebug() << "Loaded GLTF";
}

void Window::gltfBindModel()
{
	auto scene = gltfModel_.scenes[gltfModel_.defaultScene];
	vbos_.resize(gltfModel_.bufferViews.size());

	gltfBindBuffers();
	gltfBindTextures();

	// Bind model's nodes
	for (auto nodeIdx: scene.nodes) {
		gltfBindNode(nodeIdx);
	}

}

void Window::gltfBindBuffers() {
	// Generate VBOs and bind buffer views to them
	for (size_t i = 0; i < gltfModel_.bufferViews.size(); ++i) {
		auto& bufferView = gltfModel_.bufferViews[i];

		auto qname = QString::fromStdString(bufferView.name);
		qDebug() << "Binding buffer" << i << qname;

		if (bufferView.target == 0) {
			qDebug() << "Unsupported target at buffer" << i << qname;
			continue;
		}

		auto& vbo = vbos_[i];  // There's already place for the VBO in the container
		glGenBuffers(1, &vbo);   // Generate buffer, and store its ID in the container
		qDebug() << "Generated buffer for bufferView" << i;

		// Associate data with the buffer:
		auto& buffer = gltfModel_.buffers[bufferView.buffer];
		glBindBuffer(bufferView.target, vbo);
		glBufferData(bufferView.target,  // Set data
					 bufferView.byteLength,
					 buffer.data.data() + bufferView.byteOffset,
					 GL_STATIC_DRAW);

	}
}

void Window::gltfBindTextures()
{
	textures_.resize(gltfModel_.textures.size());

	for (size_t i = 0; i < gltfModel_.textures.size(); ++i) {
		auto& texture = gltfModel_.textures[i];
		qDebug() << "Binding texture" << i << QString::fromStdString(texture.name);

		if (texture.source < 0 || texture.source >= gltfModel_.images.size()) {
			qDebug() << "Invalid source, skipping...";
			continue;
		}


		auto& image = gltfModel_.images[texture.source];
		auto bufferViewIdx = image.bufferView;
		qDebug() << "Image" << texture.source << QString::fromStdString(image.name) << "with buffer view" << bufferViewIdx;


		int format;
		if (image.component == 4) {
			format = GL_RGBA;
		} else if (image.component == 3) {
			format = GL_RGB;
		} else {
			qDebug() << "Unexpected component count" << image.component << "skipping...";
			continue;
		}

		glGenTextures(1, &textures_[i]);
		glBindTexture(GL_TEXTURE_2D, textures_[i]);

		// Set texture parameters
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, format, image.pixel_type, image.image.data());
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void Window::gltfBindNode(int nodeIdx)
{
	auto& node = gltfModel_.nodes[nodeIdx];
	auto qname = QString::fromStdString(node.name);
	qDebug() << "Binding node" << nodeIdx << qname;
	if ((node.mesh >= 0) && ((size_t)node.mesh < gltfModel_.meshes.size())) {
		qDebug() << "Node" << nodeIdx << " -> Mesh" << node.mesh;
		gltfBindMesh(node.mesh);
	} else {
		qDebug() << "Node" << nodeIdx << "has no valid mesh (" << node.mesh << ")";
	}

	for (auto& childIdx: node.children) {
		qDebug() << "Node" << childIdx << " is a child of Node" << nodeIdx;
		gltfBindNode(childIdx);
	}
}

void Window::gltfBindMesh(int meshIdx)
{
	auto& mesh = gltfModel_.meshes[meshIdx];
	qDebug() << "Mesh" << meshIdx << QString::fromStdString(mesh.name);
	for (auto& primitive: mesh.primitives) {
		for (auto const& [name, accessorIdx]: primitive.attributes) {
			auto accessor = gltfModel_.accessors[accessorIdx];
			auto bufferViewIdx = accessor.bufferView;
			glBindBuffer(GL_ARRAY_BUFFER, vbos_[bufferViewIdx]);

			int vaa;
			if (name == "POSITION") vaa = 0;
			else if (name == "NORMAL") vaa = 1;
			else if (name == "TEXCOORD_0") vaa = 2;
			else {
				qDebug() << "Attribute" << QString::fromStdString(name) << "was skipped in mesh" << meshIdx;
				continue;
			}

			int size;
			if (accessor.type == TINYGLTF_TYPE_SCALAR) {
				size = 1;
			} else if (accessor.type == TINYGLTF_TYPE_VEC2) {
				size = 2;
			} else if (accessor.type == TINYGLTF_TYPE_VEC3) {
				size = 3;
			} else if (accessor.type == TINYGLTF_TYPE_VEC4) {
				size = 4;
			} else {
				qDebug() << "Unsupported accessor type: " << accessor.type;
				throw std::runtime_error("Unsupported accessor type");
			}

			auto& bufferView = gltfModel_.bufferViews[bufferViewIdx];
			int byteStride = accessor.ByteStride(bufferView);

			glEnableVertexAttribArray(vaa);
			glVertexAttribPointer(vaa, size,
								  accessor.componentType, accessor.normalized ? GL_TRUE : GL_FALSE,
								  byteStride, BUFFER_OFFSET(accessor.byteOffset));

			qDebug() << "Bound mesh" << meshIdx << "vaa" << vaa << "size" << size
					 << "bufferView" << bufferViewIdx << "Offset" << bufferView.byteOffset
					 << "Stride" << byteStride << "Component type" << accessor.componentType;
		}

	}
}

void Window::gltfRenderModel()
{
	const auto& scene = gltfModel_.scenes[gltfModel_.defaultScene];
	for (auto& nodeIdx: scene.nodes) {
		gltfRenderNode(nodeIdx);
	}
}

void Window::gltfRenderNode(int nodeIdx)
{
	auto& node = gltfModel_.nodes[nodeIdx];
	if ((node.mesh >= 0) && (node.mesh < gltfModel_.meshes.size())) {
		gltfRenderMesh(node.mesh);
	}

	for (auto& childIdx: node.children) {
		gltfRenderNode(childIdx);
	}
}

void Window::gltfRenderMesh(int meshIdx)
{
	auto& mesh = gltfModel_.meshes[meshIdx];
	for (auto& primitive: mesh.primitives) {
		auto& matrial = gltfModel_.materials[primitive.material];

		// Bind texture:
		for (auto [name, value]: matrial.values) {
			if (name == "baseColorTexture") {
				auto textureIdx = value.TextureIndex();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, textures_[textureIdx]);
				break;
			}
		}

		auto& accessor = gltfModel_.accessors[primitive.indices];
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos_[accessor.bufferView]);
		glDrawElements(primitive.mode,
					   accessor.count,
					   accessor.componentType,
					   BUFFER_OFFSET(accessor.byteOffset));

		// Unbind texture:
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void Window::render()
{
	// Update view matrix
	view_ = currentCamera_->getView();
	const auto mvp = projection_ * view_ * model_;

	// Update model matrix -- skip
	// Update normal matrix:
	auto normalMat = glm::inverse(model_);

	// Set uniforms:
	program_->setUniformValue(uniforms_.model, QMatrix4x4(glm::value_ptr(model_)).transposed());
	program_->setUniformValue(uniforms_.view, QMatrix4x4(glm::value_ptr(view_)).transposed());
	program_->setUniformValue(uniforms_.normal, QMatrix4x4(glm::value_ptr(normalMat)));
	program_->setUniformValue(uniforms_.mvp, QMatrix4x4(glm::value_ptr(mvp)).transposed());
	program_->setUniformValue(uniforms_.morph, morph_);
	program_->setUniformValue(uniforms_.enableDiffuse, enableDiffuse_);
	program_->setUniformValue(uniforms_.enableSpot, enableSpot_);

	// Render model:
	gltfRenderModel();
}
