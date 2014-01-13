/******************************************************************************
 * Copyright © 2012-2014 Institut für Nachrichtentechnik, Universität Rostock *
 * Copyright © 2006-2012 Quality & Usability Lab,                             *
 *                       Telekom Innovation Laboratories, TU Berlin           *
 *                                                                            *
 * This file is part of the SoundScape Renderer (SSR).                        *
 *                                                                            *
 * The SSR is free software:  you can redistribute it and/or modify it  under *
 * the terms of the  GNU  General  Public  License  as published by the  Free *
 * Software Foundation, either version 3 of the License,  or (at your option) *
 * any later version.                                                         *
 *                                                                            *
 * The SSR is distributed in the hope that it will be useful, but WITHOUT ANY *
 * WARRANTY;  without even the implied warranty of MERCHANTABILITY or FITNESS *
 * FOR A PARTICULAR PURPOSE.                                                  *
 * See the GNU General Public License for more details.                       *
 *                                                                            *
 * You should  have received a copy  of the GNU General Public License  along *
 * with this program.  If not, see <http://www.gnu.org/licenses/>.            *
 *                                                                            *
 * The SSR is a tool  for  real-time  spatial audio reproduction  providing a *
 * variety of rendering algorithms.                                           *
 *                                                                            *
 * http://spatialaudio.net/ssr                           ssr@spatialaudio.net *
 ******************************************************************************/

/// @file
/// Renderer base class.

#ifndef SSR_RENDERERBASE_H
#define SSR_RENDERERBASE_H

#include <string>

#include "apf/mimoprocessor.h"
#include "apf/shareddata.h"
#include "apf/container.h"  // for distribute_list()
#include "apf/parameter_map.h"
#include "apf/math.h"  // for dB2linear()

// TODO: avoid multiple ambiguous "Source" classes
#include "source.h"  // for ::Source::model_t

#include "maptools.h"

#ifndef SSR_QUERY_POLICY
#define SSR_QUERY_POLICY apf::disable_queries
#endif

namespace ssr
{

/** Renderer base class.
 * @todo more documentation!
 *
 * The parallel rendering engine uses the non-blocking datastructure RtList to
 * communicate between realtime and non-realtime threads.
 * All non-realtime accesses to RtList%s have to be locked with
 * get_scoped_lock() to ensure single-reader/single-writer operation.
 **/
template<typename Derived>
class RendererBase : public apf::MimoProcessor<Derived
                     , APF_MIMOPROCESSOR_INTERFACE_POLICY
                     , APF_MIMOPROCESSOR_THREAD_POLICY
                     , SSR_QUERY_POLICY>
{
  private:
    using _base = apf::MimoProcessor<Derived, APF_MIMOPROCESSOR_INTERFACE_POLICY
      , APF_MIMOPROCESSOR_THREAD_POLICY, SSR_QUERY_POLICY>;

  public:
    using rtlist_t = typename _base::rtlist_t;
    using Input = typename _base::DefaultInput;
    using ScopedLock = typename _base::ScopedLock;
    using sample_type = typename _base::sample_type;

    using _base::_fifo;

    class Source;
    // TODO: try to remove this:
    using SourceBase = Source;
    class Output;

    struct State
    {
      State(apf::CommandQueue& fifo)
        : reference_position(fifo)
        , reference_orientation(fifo)
        , reference_offset_position(fifo)
        , reference_offset_orientation(fifo)
        , master_volume(fifo, 1)
        , processing(fifo, true)
        , amplitude_reference_distance(fifo, 3)
      {}

      apf::SharedData<Position> reference_position;
      apf::SharedData<Orientation> reference_orientation;
      apf::SharedData<Position> reference_offset_position;
      apf::SharedData<Orientation> reference_offset_orientation;
      apf::SharedData<sample_type> master_volume;
      apf::SharedData<bool> processing;
      apf::SharedData<sample_type> amplitude_reference_distance;
    } state;

    // If you don't need a list proxy, just use a reference to the list
    template<typename L, typename ListProxy, typename DataMember>
    class AddToSublistCommand : public apf::CommandQueue::Command
    {
      public:
        AddToSublistCommand(L input, ListProxy output, DataMember member)
          : _input(input)
          , _output(output)
          , _member(member)
        {}

        virtual void execute()
        {
          apf::distribute_list(_input, _output, _member);
        }

        // Empty function, because no cleanup is necessary. 
        virtual void cleanup() {}

      private:
        L _input;
        ListProxy _output;
        DataMember _member;
    };

    template<typename L, typename ListProxy, typename DataMember>
    void add_to_sublist(const L& input, ListProxy output, DataMember member)
    {
      _fifo.push(new AddToSublistCommand<L, ListProxy, DataMember>(
            input, output, member));
    }

    template<typename L, typename ListProxy, typename DataMember>
    class RemFromSublistCommand : public apf::CommandQueue::Command
    {
      public:
        RemFromSublistCommand(const L input, ListProxy output
            , DataMember member)
          : _input(input)
          , _output(output)
          , _member(member)
        {}

        virtual void execute()
        {
          apf::undistribute_list(_input, _output, _member, _garbage);
        }

        virtual void cleanup()
        {
          // Nothing to be done. _garbage is taken care of in the destructor.
        }

      private:
        const L _input;
        ListProxy _output;
        DataMember _member;
        typename std::remove_const<L>::type _garbage;
    };

    template<typename L, typename ListProxy, typename DataMember>
    void rem_from_sublist(const L& input, ListProxy output, DataMember member)
    {
      _fifo.push(new RemFromSublistCommand<L, ListProxy, DataMember>(
            input, output, member));
    }

    int add_source(const apf::parameter_map& p = apf::parameter_map());
    void rem_source(Source* source);
    void rem_all_sources();

    Source* get_source(int id);

    // May only be used in realtime thread!
    const rtlist_t& get_source_list() const { return _source_list; }

    const std::map<int, Source*>& get_source_map() const { return _source_map; }

    sample_type get_master_level() const { return _master_level; }

    bool show_head() const { return _show_head; }

    // TODO: proper solution for getting the reproduction setup
    template<typename SomeListType>
    void get_loudspeakers(SomeListType&) {}

    std::unique_ptr<ScopedLock> get_scoped_lock()
    {
      // TODO: in C++14, use make_unique()
      return std::unique_ptr<ScopedLock>(new ScopedLock(_lock));
    }

    const sample_type master_volume_correction;  // linear

  protected:
    RendererBase(const apf::parameter_map& p);

    // TODO: make private?
    sample_type _master_level;

    rtlist_t _source_list;

    // TODO: find a better solution to get loudspeaker vs. headphone renderer
    bool _show_head;

  private:
    apf::parameter_map _add_params(const apf::parameter_map& params)
    {
      auto temp = params;
      temp.set("name", params.get("name", Derived::name()));
      return temp;
    }

    int _get_new_id();

    std::map<int, Source*> _source_map;

    int _highest_id;

    typename _base::Lock _lock;
};

/** Constructor.
 * @param p Parameters for RendererBase and MimoProcessor
 **/
template<typename Derived>
RendererBase<Derived>::RendererBase(const apf::parameter_map& p)
  : _base(_add_params(p))
  , state(_fifo)
  , master_volume_correction(apf::math::dB2linear(
        this->params.get("master_volume_correction", 0.0)))
  , _master_level()
  , _source_list(_fifo)
  , _show_head(true)
  , _highest_id(0)
{}

/** Create a new source.
 * @return ID of new source
 * @throw unknown whatever the Derived::Source constructor throws
 **/
template<typename Derived>
int RendererBase<Derived>::add_source(const apf::parameter_map& p)
{
  ScopedLock guard(_lock);

  int id = _get_new_id();

  typename Derived::Input::Params in_params;
  in_params = p;
  in_params.set("id", in_params.get("id", id));
  auto in = this->add(in_params);

  // WARNING: if Derived::Input throws an exception, the SSR crashes!

  typename Derived::Source::Params src_params;
  src_params = p;
  src_params.parent = &this->derived();
  src_params.fifo = &_fifo;
  src_params.input = in;

  typename Derived::Source* src;

  try
  {
    src = _source_list.add(new typename Derived::Source(src_params));
  }
  catch (...)
  {
    // TODO: really remove the corresponding Input?
    this->rem(in);
    throw;
  }

  // This cannot be done in the Derived::Source constructor because then the
  // connections to the Outputs are active before the Source is properly added
  // to the source list:
  src->connect();

  _source_map[id] = src;
  return id;
  // TODO: what happens on failure? can there be failure?
}

template<typename Derived>
void RendererBase<Derived>::rem_source(Source* source)
{
  assert(source);

  // TODO: remove by ID instead of by pointer?
  ScopedLock guard(_lock);

  // work-around to delete source from _source_map
  auto delinquent = std::find_if(_source_map.begin(), _source_map.end()
        , [source] (const std::pair<int, Source*>& in)
          {
            return in.second == source;
          });
  _source_map.erase(delinquent);

  source->derived().disconnect();

  auto input = const_cast<Input*>(&source->_input);
  _source_list.rem(source);

  // TODO: really remove the corresponding Input?
  // ATTENTION: there may be several sources using the input! (or not?)

  this->rem(input);
}

template<typename Derived>
void RendererBase<Derived>::rem_all_sources()
{
  while (!_source_map.empty())
  {
    this->rem_source(_source_map.begin()->second);
  }
  _highest_id = 0;
}

template<typename Derived>
typename RendererBase<Derived>::Source*
RendererBase<Derived>::get_source(int id)
{
  return maptools::get_item(_source_map, id);
}

template<typename Derived>
int
RendererBase<Derived>::_get_new_id()
{
  return ++_highest_id;
}

/// A sound source.
template<typename Derived>
class RendererBase<Derived>::Source
                  : public _base::template ProcessItem<typename Derived::Source>
                  , public apf::has_begin_and_end<typename Input::iterator>
{
  private:
    using SourceBase
      = typename _base::template ProcessItem<typename Derived::Source>;

  public:
    using sample_type
      = typename std::iterator_traits<typename Input::iterator>::value_type;

    friend class RendererBase<Derived>;  // rem_source() needs access to _input

    struct Params : apf::parameter_map
    {
      Params() : parent(0) {}
      Derived* parent;
      const typename Derived::Input* input;
      apf::CommandQueue* fifo;

      Params& operator=(const apf::parameter_map& p)
      {
        this->apf::parameter_map::operator=(p);
        return *this;
      }
    };

    explicit Source(const Params& p)
      : parent(*(p.parent ? p.parent : throw std::logic_error(
              "Bug (RendererBase::Source): parent == NULL!")))
      , position(*(p.fifo ? p.fifo : throw std::logic_error(
              "Bug (RendererBase::Source): fifo == NULL!")))
      , orientation(*p.fifo)
      , gain(*p.fifo, sample_type(1.0))
      , mute(*p.fifo, false)
      , model(*p.fifo, ::Source::point)
      , weighting_factor()
      , _input(*(p.input ? p.input : throw std::logic_error(
              "Bug (RendererBase::Source): input == NULL!")))
      , _pre_fader_level()
      , _level()
    {}

    APF_PROCESS(Source, SourceBase)
    {
      this->_begin = _input.begin();
      this->_end = _input.end();

      if (!_input.parent.state.processing || this->mute)
      {
        this->weighting_factor = 0.0;
      }
      else
      {
        this->weighting_factor = this->gain;
        // If the renderer does something nonlinear, the master volume should
        // be applied to the output signal ... TODO: shall we care?
        this->weighting_factor *= _input.parent.state.master_volume;
        this->weighting_factor *= _input.parent.master_volume_correction;
      }

      _level_helper(_input.parent);

      assert(this->weighting_factor.exactly_one_assignment());
    }

    sample_type get_level() const { return _level; }

    // In the default case, the output level are ignored
    bool get_output_levels(sample_type*, sample_type*) const { return false; }

    void connect() {}
    void disconnect() {}

    Derived& parent;

    apf::SharedData<Position> position;
    apf::SharedData<Orientation> orientation;
    apf::SharedData<sample_type> gain;
    apf::SharedData<bool> mute;
    apf::SharedData< ::Source::model_t> model;

    apf::BlockParameter<sample_type> weighting_factor;

  protected:
    const Input& _input;

  private:
    void _level_helper(apf::enable_queries&)
    {
      _pre_fader_level = apf::math::max_amplitude(_input.begin(), _input.end());
      _level = _pre_fader_level * this->weighting_factor;
    }

    void _level_helper(apf::disable_queries&) {}

    sample_type _pre_fader_level;
    sample_type _level;
};

template<typename Derived>
class RendererBase<Derived>::Output : public _base::Output
{
  public:
    Output(const typename _base::Output::Params& p)
      : _base::Output(p)
      , _level()
    {}

    struct Process : _base::Output::Process
    {
      explicit Process(Output& o) : _base::Output::Process(o) , _out(o) {}

      ~Process()
      {
        _out._level_helper(_out.parent);
      }

      private:
        Output& _out;
    };

    sample_type get_level() const { return _level; }

  protected:
    void _level_helper(apf::enable_queries&)
    {
      _level = apf::math::max_amplitude(this->buffer.begin()
          , this->buffer.end());
    }

    void _level_helper(apf::disable_queries&) {}

  private:
    sample_type _level;
};

// This is a kind of C++ mixin class, but it also includes the CRTP
template<typename Derived, template<typename> class Base>
struct SourceToOutput : Base<Derived>
{
  using Input = typename Base<Derived>::Input;

  struct Source : Base<Derived>::Source
  {
    using Params = typename Base<Derived>::Source::Params;
    using sourcechannels_t = apf::fixed_vector<typename Derived::SourceChannel>;

    template<typename... Args>
    Source(const Params& p, Args&&... args)
      : Base<Derived>::Source(p)
      , sourcechannels(std::forward<Args>(args)...)
    {}

    void connect()
    {
      auto temp = std::list<typename Derived::SourceChannel*>();
      apf::append_pointers(this->sourcechannels, temp);
      this->parent.add_to_sublist(temp, apf::make_cast_proxy<Output>(
            const_cast<rtlist_t&>(this->parent.get_output_list()))
          , &Output::sourcechannels);
    }

    void disconnect()
    {
      auto temp = std::list<typename Derived::SourceChannel*>();
      apf::append_pointers(this->sourcechannels, temp);
      this->parent.rem_from_sublist(temp, apf::make_cast_proxy<Output>(
            const_cast<rtlist_t&>(this->parent.get_output_list()))
          , &Output::sourcechannels);
    }

    sourcechannels_t sourcechannels;

    private:
      using rtlist_t = typename Derived::rtlist_t;
  };

  struct Output : Base<Derived>::Output
  {
    using Params = typename Base<Derived>::Output::Params;
    using sourcechannels_t = std::list<typename Derived::SourceChannel*>;

    Output(const Params& p) : Base<Derived>::Output(p) {}

    sourcechannels_t sourcechannels;
  };

  explicit SourceToOutput(const apf::parameter_map& params)
    : Base<Derived>(params)
  {}
};

}  // namespace ssr

#endif

// Settings for Vim (http://www.vim.org/), please do not remove:
// vim:softtabstop=2:shiftwidth=2:expandtab:textwidth=80:cindent
// vim:fdm=expr:foldexpr=getline(v\:lnum)=~'/\\*\\*'&&getline(v\:lnum)!~'\\*\\*/'?'a1'\:getline(v\:lnum)=~'\\*\\*/'&&getline(v\:lnum)!~'/\\*\\*'?'s1'\:'='
