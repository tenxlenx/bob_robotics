#include "common.h"

// BoB robotics includes
#include "navigation/image_database.h"
#include "navigation/infomax.h"
#include "navigation/perfect_memory.h"

// Standard C++ includes
#include <stdexcept>

namespace {

template<class T>
using optional = std::experimental::optional<T>;

namespace py = pybind11;
using namespace py::literals;
using namespace BoBRobotics::Navigation;
using namespace units::angle;

using PerfectMemoryType = PerfectMemoryRotater<>;
using InfoMaxType = InfoMaxRotater<>;

namespace {
static py::module numpy = py::module::import("numpy");
static py::function atLeast2d = numpy.attr("atleast_2d");
} // anonymous namespace

template<class T>
class PyAlgoWrapperBase
{
public:
    template<class... Ts>
    PyAlgoWrapperBase(Ts&&... args)
      : m_Algo(std::forward<Ts>(args)...)
    {}

    auto getHeading(const cv::Mat &img) const
    {
        return std::get<0>(getRIDFData(img));
    }

    auto getRIDFData(const cv::Mat &img) const
    {
        return m_Algo.getHeading(img);
    }

    auto test(const cv::Mat &img) const
    {
        return m_Algo.test(img);
    }

    void train(py::object imageSet)
    {
        const py::array npArray = atLeast2d(imageSet);
        switch (npArray.ndim()) {
        case 2:
            m_Algo.train(npArray.cast<cv::Mat>());
            break;
        case 3:
            ranges::for_each(toRange<cv::Mat>(npArray), [this](const auto &im) { m_Algo.train(im); });
            break;
        default : throw std::invalid_argument("Wrong number of dimensions");
        }
    }

protected:
    T m_Algo;
};

template<class T>
class PyAlgoWrapper
  : public PyAlgoWrapperBase<T>
{
public:
    template<class... Ts>
    PyAlgoWrapper(Ts&&... args)
      : PyAlgoWrapperBase<T>(std::forward<Ts>(args)...)
    {}
};


template<>
class PyAlgoWrapper<InfoMaxType>
  : public PyAlgoWrapperBase<InfoMaxType>
{
public:
    PyAlgoWrapper(const cv::Size &size, float learningRate,
                  float tanhScalingFactor, Normalisation normalisation,
                  Eigen::MatrixXf weights)
      : PyAlgoWrapperBase<InfoMaxType>(size, learningRate, tanhScalingFactor,
                                       normalisation, std::move(weights))
    {}

    PyAlgoWrapper(const cv::Size &size, float learningRate,
                  float tanhScalingFactor, Normalisation normalisation)
      : PyAlgoWrapper(size, learningRate, tanhScalingFactor, normalisation,
                      generateInitialWeights(size).first)
    {}

    const auto &getWeights() const { return m_Algo.getWeights(); }

    static std::pair<Eigen::MatrixXf, unsigned>
    generateInitialWeights(const cv::Size &size,
                           const optional<int> &numHidden = std::experimental::nullopt,
                           const optional<unsigned> &seedArg = std::experimental::nullopt)
    {
        const auto seed = seedArg ? seedArg.value() : std::random_device()();
        const auto numInput = size.width * size.height;
        auto weights = InfoMax<>::generateInitialWeights(numInput,
                                                         numHidden.value_or(numInput),
                                                         seed);
        return std::make_pair(weights, seed);
    }
};

template<class Algo>
auto
addAlgo(py::handle scope, const char *name)
{
    using T = PyAlgoWrapper<Algo>;
    return py::class_<T>(scope, name)
            .def("get_heading", &T::getHeading)
            .def("get_ridf_data", &T::getRIDFData)
            .def("test", &T::test)
            .def("train", &T::train);
}
} // anonymous namespace

namespace BoBRobotics {
namespace Navigation {
void addAlgorithmClasses(py::module &m)
{
    py::enum_<Normalisation>(m, "Normalisation")
            .value("none", Normalisation::None)
            .value("zscore", Normalisation::ZScore);

    // Add various algorithms as Python classes
    addAlgo<PerfectMemoryType>(m, "PerfectMemory")
            .def(py::init<const cv::Size &>());
    addAlgo<InfoMaxType>(m, "InfoMax")
            .def(py::init<const cv::Size &, float, float, Normalisation>(),
                 "size"_a,
                 "learning_rate"_a = InfoMaxType::DefaultLearningRate,
                 "tanh_scaling_factor"_a = InfoMaxType::DefaultTanhScalingFactor,
                 "normalisation"_a = Normalisation::None)
            .def(py::init<const cv::Size &, float, float, Normalisation, Eigen::MatrixXf>(),
                 "size"_a,
                 "learning_rate"_a = InfoMaxType::DefaultLearningRate,
                 "tanh_scaling_factor"_a = InfoMaxType::DefaultTanhScalingFactor,
                 "normalisation"_a = Normalisation::None,
                 "weights"_a)
            .def("get_weights", &PyAlgoWrapper<InfoMaxType>::getWeights)
            .def_static("generate_initial_weights",
                        &PyAlgoWrapper<InfoMaxType>::generateInitialWeights,
                        "size"_a,
                        "num_hidden"_a = ::optional<int>{},
                        "seed"_a = ::optional<unsigned>{})
            .def_property_readonly_static("DEFAULT_LEARNING_RATE", [](const py::object &) { return InfoMaxType::DefaultLearningRate; })
            .def_property_readonly_static("DEFAULT_TANH_SCALING_FACTOR", [](const py::object &) { return InfoMaxType::DefaultTanhScalingFactor; });
}
} // Navigation
} // BoBRobotics
