<?php

/**
 * @file
 * Contains SearchApiViewsHandlerFilterDate.
 */

/**
 * Views filter handler base class for handling date fields.
 */
class SearchApiViewsHandlerFilterDate extends SearchApiViewsHandlerFilterNumeric {

  /**
   * Add a "widget type" option.
   */
  public function option_definition() {
    return parent::option_definition() + array(
      'widget_type' => array('default' => 'default'),
      'date_popup_format' => array('default' => 'm/d/Y'),
      'year_range' => array('default' => '-3:+3'),
    );
  }

  /**
   * If the date popup module is enabled, provide the extra option setting.
   */
  public function has_extra_options() {
    if (module_exists('date_popup')) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Add extra options if we allow the date popup widget.
   */
  public function extra_options_form(&$form, &$form_state) {
    parent::extra_options_form($form, $form_state);

    if (module_exists('date_popup')) {
      $widget_options = array(
        'default' => 'Default',
        'date_popup' => 'Date popup',
      );
      $form['widget_type'] = array(
        '#type' => 'radios',
        '#title' => t('Date selection form element'),
        '#default_value' => $this->options['widget_type'],
        '#options' => $widget_options,
      );
      $form['date_popup_format'] = array(
        '#type' => 'textfield',
        '#title' => t('Date format'),
        '#default_value' => $this->options['date_popup_format'],
        '#description' => t('A date in any format understood by <a href="@doc-link">PHP</a>. For example, "Y-m-d" or "m/d/Y".', array(
          '@doc-link' => 'http://php.net/manual/en/function.date.php',
        )),
        '#states' => array(
          'visible' => array(
            ':input[name="options[widget_type]"]' => array('value' => 'date_popup'),
          ),
        ),
      );
    }

    if (module_exists('date_api')) {
      $form['year_range'] = array(
        '#type' => 'date_year_range',
        '#default_value' => $this->options['year_range'],
      );
    }
  }

  /**
   * Validate extra options.
   */
  public function extra_options_validate($form, &$form_state) {
    if (isset($form_state['values']['options']['year_range'])) {
      if (!preg_match('/^(?:\-[0-9]{1,4}|[0-9]{4}):(?:[\+|\-][0-9]{1,4}|[0-9]{4})$/', $form_state['values']['options']['year_range'])) {
        form_error($form['year_range'], t('Date year range must be in the format -9:+9, 2005:2010, -9:2010, or 2005:+9'));
      }
    }
  }

  /**
   * Provide a form for setting the filter value.
   */
  public function value_form(&$form, &$form_state) {
    parent::value_form($form, $form_state);

    $is_date_popup = ($this->options['widget_type'] == 'date_popup' && module_exists('date_popup'));

    // If the operator is between
    if ($this->operator == 'between') {
      if ($is_date_popup) {
        $form['value']['min']['#type'] = 'date_popup';
        $form['value']['min']['#date_format'] =  $this->options['date_popup_format'];
        $form['value']['min']['#date_year_range'] = $this->options['year_range'];
        $form['value']['max']['#type'] = 'date_popup';
        $form['value']['max']['#date_format'] =  $this->options['date_popup_format'];
        $form['value']['max']['#date_year_range'] = $this->options['year_range'];
      }
    }
    // If we are using the date popup widget, overwrite the settings of the form
    // according to what date_popup expects.
    elseif ($is_date_popup) {
      // Add an "id" for the "value" field, since it is expected in
      // date_views_form_views_exposed_form_alter().
      // @see date_views_filter_handler_simple::value_form()
      $form['value']['#id'] = 'date_views_exposed_filter-' . bin2hex(backdrop_random_bytes(16));
      $form['value']['#type'] = 'date_popup';
      $form['value']['#date_format'] =  $this->options['date_popup_format'];
      $form['value']['#date_year_range'] = $this->options['year_range'];
      unset($form['value']['#description']);
    }
    elseif (empty($form_state['exposed'])) {
      $form['value']['#description'] = t('A date in any format understood by <a href="@doc-link">PHP</a>. For example, "@date1" or "@date2".', array(
        '@doc-link' => 'http://php.net/manual/en/function.strtotime.php',
        '@date1' => format_date(REQUEST_TIME, 'custom', 'Y-m-d H:i:s'),
        '@date2' => 'now + 1 day',
      ));
    }
  }

  /**
   * Add this filter to the query.
   */
  public function query() {
    $this->normalizeValue();

    if ($this->operator === 'empty') {
      $this->query->condition($this->real_field, NULL, '=', $this->options['group']);
    }
    elseif ($this->operator === 'not empty') {
      $this->query->condition($this->real_field, NULL, '<>', $this->options['group']);
    }
    elseif (in_array($this->operator, array('between', 'not between'), TRUE)) {
      $min = $this->value['min'];
      if ($min !== '') {
        $min = is_numeric($min) ? $min : strtotime($min, REQUEST_TIME);
      }
      $max = $this->value['max'];
      if ($max !== '') {
        $max = is_numeric($max) ? $max : strtotime($max, REQUEST_TIME);
      }

      if (is_numeric($min) && is_numeric($max)) {
        $this->query->condition($this->real_field, array($min, $max), strtoupper($this->operator), $this->options['group']);
      }
      elseif (is_numeric($min)) {
        $operator = $this->operator === 'between' ? '>=' : '<';
        $this->query->condition($this->real_field, $min, $operator, $this->options['group']);
      }
      elseif (is_numeric($max)) {
        $operator = $this->operator === 'between' ? '<=' : '>';
        $this->query->condition($this->real_field, $min, $operator, $this->options['group']);
      }
    }
    else {
      $v = is_numeric($this->value['value']) ? $this->value['value'] : strtotime($this->value['value'], REQUEST_TIME);
      if ($v !== FALSE) {
        $this->query->condition($this->real_field, $v, $this->operator, $this->options['group']);
      }
    }
  }

}
